/*
 * radio — lifecycle/control core (RADIO-1b/RADIO-2): owns session creation
 * and teardown, the async command queue, and status/telemetry snapshots.
 * The network fetch and decode workers this starts live alongside it:
 * radio_ring.c (compressed + PCM ring buffers), radio_stream.c (HTTP/ICY
 * fetch, stream_task), radio_decode.c (decoder_task). See radio.h and
 * radio_internal.h.
 *
 * RH-S3-02: session-based lifecycle with monotonically increasing generation,
 * _Atomic stop_requested flag, event-group exit acknowledgement, and control
 * mutex for lifecycle transitions.
 */
#include "radio_internal.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"

#include "esp_audio_simple_dec_default.h"
#include "esp_audio_dec_default.h"

static const char *TAG = "radio";

/* Decoded-PCM ring sizing (radio_ring.c owns the storage; radio_init() here
 * decides how big it is). 4 bytes/frame @ 44100 Hz: 1 MiB ~= 5.9 s of
 * decoded audio — a deep jitter buffer so a multi-second TCP/WiFi stall
 * drains the cushion instead of the output. Playback is gated (g_radio_prebuffered)
 * until the ring first fills to the prebuffer threshold (~3 s by default),
 * and re-gated if it ever fully drains, so recovery re-buffers cleanly
 * rather than restarting choppy. */
#define PCM_RING_BYTES      (1024 * 1024)
#define NVS_NS_RADIO        "radio"
#define NVS_KEY_PREBUF      "prebuf_ms"

static esp_err_t radio_prebuffer_load(void);

/* 7.1: forward declarations for internal sync play/stop.
   Static for device builds (cmd-worker only). Non-static for host tests. */
#ifndef UNIT_TEST
static esp_err_t radio_play_sync(const char *playlist_or_url);
static esp_err_t radio_stop_sync(void);
#endif

/* ---- Command queue (RH-S3-09) ---- */

/* Command types sent to the worker task. */
typedef enum {
    RADIO_CMD_PLAY = 0,
    RADIO_CMD_STOP,
} radio_cmd_type_t;

/* Command object — sent via queue to the worker. */
typedef struct {
    radio_cmd_type_t type;
    char url[RADIO_URL_MAX];
} radio_cmd_t;

static QueueHandle_t s_radio_cmd_q;  /* command queue */
static TaskHandle_t  s_radio_cmd_task; /* worker task handle */
static _Atomic bool  s_cmd_shutdown; /* shutdown flag for worker */

/* 7.10: module-level event group — command-worker exit acknowledgement.
 * Distinct from any session's per-play event group, and outlives sessions. */
EventGroupHandle_t g_radio_module_events;

/* 7.4: destroy a session only after both workers have exited. configASSERT
 * enforces this is never called otherwise — every call site below checks
 * session_all_exited()/session_join() first, so a false assertion here
 * means a genuine lifecycle bug, not a recoverable condition. */
static void session_destroy_joined(radio_session_t *s)
{
    if (!s) return;
    configASSERT(session_all_exited(s));
    ESP_LOGI(TAG, "joining session gen=%" PRIu32, s->generation);
    vEventGroupDelete(s->events);
    free(s);
}

/* Module state protected by g_radio_control_mtx. */
SemaphoreHandle_t g_radio_control_mtx;
static radio_session_t *s_active_session;
radio_state_t g_radio_state;
static uint32_t s_next_generation;

/* 7.8: only the generation that is still s_active_session's may mutate
 * g_radio_state — a stale worker from a session already stopped/replaced
 * can't clobber a newer session's state. */
void radio_set_state_for_generation(uint32_t generation, radio_state_t state)
{
    xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
    if (s_active_session && s_active_session->generation == generation) {
        g_radio_state = state;
    }
    xSemaphoreGive(g_radio_control_mtx);
}

/* 7.8: BUFFERING -> RUNNING once both READY bits are set, gated by
 * generation so a stale worker can't promote a session that's no longer
 * current. Called by stream_task/decoder_task right after each sets its
 * own READY bit. */
void radio_try_publish_running(radio_session_t *s)
{
    if (!s || !s->events) return;
    EventBits_t bits = xEventGroupGetBits(s->events);
    if ((bits & RADIO_EVT_ALL_READY) != RADIO_EVT_ALL_READY) return;

    xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
    if (s_active_session == s && s_active_session->generation == s->generation &&
        g_radio_state == RADIO_STATE_BUFFERING) {
        g_radio_state = RADIO_STATE_RUNNING;
    }
    xSemaphoreGive(g_radio_control_mtx);
}

/* ---- Test injection hooks (RH-S3-02) ---- */
/* Expose event bits for failure injection tests (match above). */

/* Set exit bits on the active session's event group for test injection. */
void radio_test_inject_exit_bits(EventBits_t bits)
{
    if (s_active_session && s_active_session->events) {
        xEventGroupSetBits(s_active_session->events, bits);
    }
}

/* Return the current active session pointer for test inspection. */
void *radio_test_get_active_session(void)
{
    return s_active_session;
}

#ifdef UNIT_TEST
/* 7.10: the mocked command-worker task body never runs in host tests, so
 * nothing else would ever set its module-level exit-acknowledgement bit. */
void radio_test_inject_cmd_exited(void)
{
    if (g_radio_module_events) {
        xEventGroupSetBits(g_radio_module_events, RADIO_MODULE_EVT_CMD_EXITED);
    }
}
#endif

/* ---- Telemetry (protected by g_radio_ring_mtx, defined in radio_ring.c) ---- */
char           g_radio_url[RADIO_URL_MAX];
char           g_radio_station[RADIO_NAME_MAX];
char           g_radio_title[RADIO_TITLE_MAX];
radio_codec_t  g_radio_codec;
int            g_radio_bitrate, g_radio_http_status;
uint64_t       g_radio_bytes_in;
uint32_t       g_radio_reconnects, g_radio_overflow;

/* decoder telemetry */
int            g_radio_dec_rate, g_radio_dec_ch;
uint32_t       g_radio_decode_errors;

/* Last error tracking */
radio_err_t    g_radio_last_error;
char           g_radio_last_error_detail[64];

/* ---- Public API ---- */

/* Codec name string */
const char *radio_codec_str(radio_codec_t c)
{
    switch (c) {
    case RADIO_CODEC_MP3: return "mp3";
    case RADIO_CODEC_AAC: return "aac";
    default: return "unknown";
    }
}

/* 7.6: Teardown: release all ring buffers and mutexes. Safe to call when
 * not initialized (no-op, returns ESP_OK). If a session is active and
 * fails to stop/join, or the command worker fails to acknowledge shutdown,
 * returns ESP_ERR_TIMEOUT and retains every resource untouched — never
 * dereferences a session that a prior step may already have freed. */
esp_err_t radio_deinit(void)
{
    if (!g_radio_control_mtx) return ESP_OK;

    /* 1-2: stop/join any active session first. radio_stop_sync() itself
     * frees the session on success (via session_destroy_joined()) — this
     * function must never touch the pointer again afterward. On timeout,
     * radio_stop_sync() leaves the session attached as FAULTED_JOIN_PENDING
     * and owned by s_active_session; retain everything and return. */
    esp_err_t stop_err = radio_stop_sync();
    if (stop_err != ESP_OK) return stop_err;

    /* 3-5: signal command-worker shutdown and wait for its exit
     * acknowledgement bit (7.10) instead of polling the handle. */
    if (s_radio_cmd_q && s_radio_cmd_task) {
        atomic_store(&s_cmd_shutdown, true);
        radio_cmd_t dummy = { .type = RADIO_CMD_STOP };
        xQueueSend(s_radio_cmd_q, &dummy, 0);

        EventBits_t bits = xEventGroupWaitBits(
            g_radio_module_events, RADIO_MODULE_EVT_CMD_EXITED,
            pdTRUE, pdTRUE, pdMS_TO_TICKS(RADIO_CMD_EXIT_TIMEOUT_MS));
        if ((bits & RADIO_MODULE_EVT_CMD_EXITED) != RADIO_MODULE_EVT_CMD_EXITED) {
            /* Worker didn't confirm exit — retain queue/mutex/rings/task
             * untouched so a caller can retry rather than tearing down
             * state a still-running worker might reference. */
            atomic_store(&s_cmd_shutdown, false);
            ESP_LOGW(TAG, "radio_deinit: command worker exit timeout");
            printf("DIAG|RADIO|DEINIT_TIMEOUT|reason=cmd_worker\n");
            fflush(stdout);
            return ESP_ERR_TIMEOUT;
        }
        /* Lifecycle owner clears the handle only after observing the bit —
         * the worker itself never touches it (7.10). */
        s_radio_cmd_task = NULL;
    }

    /* 6-8: delete queue, mutexes, event group, and free rings. */
    if (s_radio_cmd_q) {
        vQueueDelete(s_radio_cmd_q);
        s_radio_cmd_q = NULL;
    }
    if (g_radio_module_events) {
        vEventGroupDelete(g_radio_module_events);
        g_radio_module_events = NULL;
    }
    if (g_radio_control_mtx) {
        vSemaphoreDelete(g_radio_control_mtx);
        g_radio_control_mtx = NULL;
    }
    if (g_radio_pcm_mtx) {
        vSemaphoreDelete(g_radio_pcm_mtx);
        g_radio_pcm_mtx = NULL;
    }
    if (g_radio_ring_mtx) {
        vSemaphoreDelete(g_radio_ring_mtx);
        g_radio_ring_mtx = NULL;
    }
    if (g_radio_pcm) {
        heap_caps_free(g_radio_pcm);
        g_radio_pcm = NULL;
        g_radio_pcm_cap = 0;
        g_radio_pcm_head = g_radio_pcm_tail = g_radio_pcm_count = 0;
    }
    if (g_radio_ring) {
        heap_caps_free(g_radio_ring);
        g_radio_ring = NULL;
        g_radio_ring_cap = 0;
        g_radio_ring_head = g_radio_ring_tail = g_radio_ring_count = 0;
    }

    /* 9: reset globals. */
    g_radio_prebuffered = false;
    g_radio_state = RADIO_STATE_STOPPED;
    s_active_session = NULL;
    s_cmd_shutdown = false;
    return ESP_OK;
}

/* Command queue worker (RH-S3-09). */
static void radio_cmd_task(void *arg)
{
    (void)arg;
    radio_cmd_t cmd;
    for (;;) {
        if (xQueueReceive(s_radio_cmd_q, &cmd, portMAX_DELAY) != pdTRUE) continue;
        if (atomic_load(&s_cmd_shutdown)) break;  /* shutdown signal */
        switch (cmd.type) {
        case RADIO_CMD_PLAY:
            radio_play_sync(cmd.url);
            break;
        case RADIO_CMD_STOP:
            radio_stop_sync();
            break;
        default:
            ESP_LOGW(TAG, "unknown cmd type %d", cmd.type);
            break;
        }
    }
    /* 7.10: set the exit-acknowledgement bit and self-delete. The lifecycle
     * owner (radio_deinit()) clears s_radio_cmd_task only after observing
     * this bit — the worker itself never touches its own handle. */
    xEventGroupSetBits(g_radio_module_events, RADIO_MODULE_EVT_CMD_EXITED);
    vTaskDelete(NULL);
}

esp_err_t radio_play_async(const char *playlist_or_url)
{
    if (!s_radio_cmd_q) return ESP_ERR_INVALID_STATE;
    radio_cmd_t cmd = { .type = RADIO_CMD_PLAY };
    strlcpy(cmd.url, playlist_or_url, sizeof(cmd.url));
    if (xQueueSend(s_radio_cmd_q, &cmd, 0) == pdPASS) return ESP_OK;
    return ESP_ERR_TIMEOUT;
}

esp_err_t radio_stop_async(void)
{
    if (!s_radio_cmd_q) return ESP_ERR_INVALID_STATE;
    radio_cmd_t cmd = { .type = RADIO_CMD_STOP };
    if (xQueueSend(s_radio_cmd_q, &cmd, 0) == pdPASS) return ESP_OK;
    return ESP_ERR_TIMEOUT;
}

/* 7.9: all-or-nothing init. Every allocation/object created here is either
 * fully published (all globals set) or fully unwound on any failure — no
 * generic malloc() fallback for the PSRAM ring buffers (a fallback would
 * silently run the deep jitter rings out of scarce internal DRAM instead of
 * failing loudly), and heap_caps_free() is used consistently for every
 * heap_caps_malloc()'d buffer. */
esp_err_t radio_init(size_t ring_bytes)
{
    /* Check if already initialized — reject double-init. */
    if (g_radio_ring || g_radio_control_mtx) return ESP_ERR_INVALID_STATE;
    if (ring_bytes == 0) return ESP_ERR_INVALID_ARG;

    uint8_t *ring = heap_caps_malloc(ring_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ring) return ESP_ERR_NO_MEM;

    uint8_t *pcm = heap_caps_malloc(PCM_RING_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pcm) {
        heap_caps_free(ring);
        return ESP_ERR_NO_MEM;
    }

    SemaphoreHandle_t control_mtx = xSemaphoreCreateMutex();
    SemaphoreHandle_t ring_mtx = xSemaphoreCreateMutex();
    SemaphoreHandle_t pcm_mtx = xSemaphoreCreateMutex();
    EventGroupHandle_t module_events = xEventGroupCreate();
    if (!control_mtx || !ring_mtx || !pcm_mtx || !module_events) {
        if (control_mtx) vSemaphoreDelete(control_mtx);
        if (ring_mtx) vSemaphoreDelete(ring_mtx);
        if (pcm_mtx) vSemaphoreDelete(pcm_mtx);
        if (module_events) vEventGroupDelete(module_events);
        heap_caps_free(pcm);
        heap_caps_free(ring);
        return ESP_ERR_NO_MEM;
    }

    /* Publish the module event group before creating the command worker —
     * the worker's own exit-acknowledgement bit (7.10) targets this exact
     * object, so it must already be the live g_radio_module_events by the
     * time xTaskCreate() runs (and, in host tests, by the time a test hook
     * fired from a mocked xTaskCreate() could touch it). */
    g_radio_module_events = module_events;

    /* Command queue (RH-S3-09): serializes play/stop through a single worker. */
    QueueHandle_t cmd_q = xQueueCreate(4, sizeof(radio_cmd_t));
    if (!cmd_q) {
        g_radio_module_events = NULL;
        vSemaphoreDelete(control_mtx);
        vSemaphoreDelete(ring_mtx);
        vSemaphoreDelete(pcm_mtx);
        vEventGroupDelete(module_events);
        heap_caps_free(pcm);
        heap_caps_free(ring);
        return ESP_ERR_NO_MEM;
    }

    /* Publish every global the command worker's very first statement reads
     * BEFORE creating it — on real hardware the new task can start running
     * (and call xQueueReceive(s_radio_cmd_q, ...)) before xTaskCreate()
     * even returns to this function, so s_radio_cmd_q must already be live.
     * A single-threaded host test can't expose this ordering bug, but a
     * real scheduler preempting immediately after xTaskCreate() will. */
    g_radio_control_mtx = control_mtx;
    g_radio_ring_mtx = ring_mtx;
    g_radio_pcm_mtx = pcm_mtx;
    g_radio_ring_cap = ring_bytes;
    g_radio_ring = ring;
    g_radio_pcm_cap = PCM_RING_BYTES;
    g_radio_pcm = pcm;
    s_radio_cmd_q = cmd_q;

    TaskHandle_t cmd_task = NULL;
    if (xTaskCreate(radio_cmd_task, "radio_cmd", 4096, NULL,
                     tskIDLE_PRIORITY + 2, &cmd_task) != pdPASS) {
        g_radio_module_events = NULL;
        g_radio_control_mtx = NULL;
        g_radio_ring_mtx = NULL;
        g_radio_pcm_mtx = NULL;
        g_radio_ring = NULL;
        g_radio_ring_cap = 0;
        g_radio_pcm = NULL;
        g_radio_pcm_cap = 0;
        s_radio_cmd_q = NULL;
        vQueueDelete(cmd_q);
        vSemaphoreDelete(control_mtx);
        vSemaphoreDelete(ring_mtx);
        vSemaphoreDelete(pcm_mtx);
        vEventGroupDelete(module_events);
        heap_caps_free(pcm);
        heap_caps_free(ring);
        ESP_LOGE(TAG, "radio_cmd_task creation failed");
        return ESP_ERR_NO_MEM;
    }
    s_radio_cmd_task = cmd_task;

    /* 7.11: a non-NOT_FOUND load error is non-fatal (defaults already in
     * effect) but must be visible, not silently treated as success. */
    esp_err_t prebuf_err = radio_prebuffer_load();
    if (prebuf_err != ESP_OK) {
        ESP_LOGW(TAG, "prebuffer load failed (%s); using default %d ms",
                 esp_err_to_name(prebuf_err), PREBUF_MS_DEFAULT);
        printf("DIAG|RADIO|PREBUF_LOAD_FAILED|err=%s\n", esp_err_to_name(prebuf_err));
        fflush(stdout);
    }
    esp_audio_dec_register_default();          /* low-level MP3/AAC decoders */
    esp_audio_simple_dec_register_default();   /* simple/frame-parser wrappers */
    return ESP_OK;
}

/* 7.1: Internal synchronous play/stop — only the cmd worker calls these.
   Conditionally exported for host tests (UNIT_TEST). */
#ifndef UNIT_TEST
static
#endif
esp_err_t radio_play_sync(const char *playlist_or_url)
{
    if (!playlist_or_url || !playlist_or_url[0]) return ESP_ERR_INVALID_ARG;
    if (!g_radio_ring || !g_radio_pcm || !g_radio_control_mtx) return ESP_ERR_INVALID_STATE;

    /* Block restart while FAULTED — the faulted session must be stopped
     * explicitly before a new play can proceed. */
    radio_state_t state = radio_get_state();
    if (state == RADIO_STATE_FAULTED || state == RADIO_STATE_FAULTED_JOIN_PENDING) {
        ESP_LOGE(TAG, "cannot play while FAULTED; call radio_stop_sync() first");
        return ESP_ERR_INVALID_STATE;
    }

    /* Stop any previous session. */
    esp_err_t err = radio_stop_sync();
    if (err != ESP_OK) return err;

    /* Resolve URL. */
    char resolved[RADIO_URL_MAX];
    resolve_url(playlist_or_url, resolved, sizeof(resolved));

    /* Allocate session. */
    radio_session_t *s = calloc(1, sizeof(*s));
    if (!s) return ESP_ERR_NO_MEM;

    s->events = xEventGroupCreate();
    if (!s->events) {
        free(s);
        return ESP_ERR_NO_MEM;
    }

    /* Lock control to assign generation and URL. */
    xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
    s_next_generation++;
    s->generation = s_next_generation;
    strlcpy(s->url, resolved, sizeof(s->url));
    g_radio_state = RADIO_STATE_STARTING;
    atomic_store_explicit(&s->stop_requested, false, memory_order_relaxed);
    xSemaphoreGive(g_radio_control_mtx);

    /* Reset shared rings — safe because old session is gone (radio_stop() joined). */
    xSemaphoreTake(g_radio_ring_mtx, portMAX_DELAY);
    g_radio_ring_head = g_radio_ring_tail = g_radio_ring_count = 0;
    strlcpy(g_radio_url, resolved, sizeof(g_radio_url));
    g_radio_title[0] = g_radio_station[0] = '\0';
    g_radio_bytes_in = 0; g_radio_overflow = 0; g_radio_reconnects = 0;
    g_radio_codec = RADIO_CODEC_UNKNOWN; g_radio_bitrate = 0; g_radio_http_status = 0;
    g_radio_decode_errors = 0; g_radio_dec_rate = 0; g_radio_dec_ch = 0;
    g_radio_last_error = RADIO_ERR_NONE;
    g_radio_last_error_detail[0] = '\0';
    xSemaphoreGive(g_radio_ring_mtx);
    xSemaphoreTake(g_radio_pcm_mtx, portMAX_DELAY);
    g_radio_pcm_head = g_radio_pcm_tail = g_radio_pcm_count = 0;
    g_radio_prebuffered = false;
    xSemaphoreGive(g_radio_pcm_mtx);

    /* Create stream task. Nothing has started yet — safe to free directly
     * on failure (7.7). */
    if (xTaskCreate(stream_task, "radio", 6144, s, tskIDLE_PRIORITY + 4,
                     &s->stream_task) != pdPASS) {
        vEventGroupDelete(s->events);
        free(s);
        xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
        g_radio_state = RADIO_STATE_STOPPED;
        xSemaphoreGive(g_radio_control_mtx);
        return ESP_ERR_NO_MEM;
    }

    /* Create decoder task. 7.7: the stream task is already running — do not
     * unconditionally free the session. Synthesize DECODER_EXITED (it never
     * started, so it has trivially "exited") and join on STREAM_EXITED via
     * the same session_join()/session_destroy_joined() used everywhere
     * else. If the stream doesn't confirm exit within the timeout, attach
     * the session as active JOIN_PENDING so it remains owned/recoverable
     * instead of freeing memory a running task might still reference. */
    if (xTaskCreate(decoder_task, "radio_dec", 8192, s, tskIDLE_PRIORITY + 4,
                     &s->decoder_task) != pdPASS) {
        xEventGroupSetBits(s->events, RADIO_EVT_DECODER_EXITED);
        atomic_store_explicit(&s->stop_requested, true, memory_order_release);
        if (s->stream_task) xTaskNotifyGive(s->stream_task);

        esp_err_t join_err = session_join(s, pdMS_TO_TICKS(RADIO_STOP_TIMEOUT_MS));
        if (join_err == ESP_OK) {
            session_destroy_joined(s);
            xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
            g_radio_state = RADIO_STATE_STOPPED;
            xSemaphoreGive(g_radio_control_mtx);
        } else {
            xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
            s_active_session = s;
            g_radio_state = RADIO_STATE_FAULTED_JOIN_PENDING;
            xSemaphoreGive(g_radio_control_mtx);
            set_radio_error(RADIO_ERR_STOP_TIMEOUT, "decoder create failed, stream join timeout");
            ESP_LOGW(TAG, "decoder create failed and stream join timed out (gen=%" PRIu32 ")",
                     s->generation);
        }
        return ESP_ERR_NO_MEM;
    }

    /* 7.8: both workers created — wait for both to confirm ENTERED before
     * publishing anything beyond STARTING. A timeout (a worker hung before
     * entering) or an early exit both fail this wait identically — either
     * way startup didn't complete, so join and don't publish BUFFERING for
     * a session that isn't actually alive on both sides. */
    EventBits_t bits = xEventGroupWaitBits(
        s->events, RADIO_EVT_ALL_ENTERED, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(RADIO_STOP_TIMEOUT_MS));

    if ((bits & RADIO_EVT_ALL_ENTERED) != RADIO_EVT_ALL_ENTERED) {
        atomic_store_explicit(&s->stop_requested, true, memory_order_release);
        if (s->stream_task) xTaskNotifyGive(s->stream_task);
        if (s->decoder_task) xTaskNotifyGive(s->decoder_task);
        esp_err_t join_err = session_join(s, pdMS_TO_TICKS(RADIO_STOP_TIMEOUT_MS));
        if (join_err == ESP_OK) {
            session_destroy_joined(s);
            xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
            g_radio_state = RADIO_STATE_STOPPED;
            xSemaphoreGive(g_radio_control_mtx);
        } else {
            xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
            s_active_session = s;
            g_radio_state = RADIO_STATE_FAULTED_JOIN_PENDING;
            xSemaphoreGive(g_radio_control_mtx);
            set_radio_error(RADIO_ERR_STOP_TIMEOUT, "worker(s) never entered, join timeout");
        }
        return ESP_FAIL;
    }

    /* Both entered — publish BUFFERING now; RUNNING follows asynchronously
     * once both READY bits are set (radio_try_publish_running(), called by
     * the workers themselves). */
    xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
    s_active_session = s;
    g_radio_state = RADIO_STATE_BUFFERING;
    xSemaphoreGive(g_radio_control_mtx);

    ESP_LOGI(TAG, "play gen=%" PRIu32 ": %s", s->generation, resolved);
    printf("DIAG|RADIO|START|gen=%" PRIu32 ",url=%s\n", s->generation, resolved);
    fflush(stdout);
    return ESP_OK;
}

/* 7.1: Internal synchronous stop — only the cmd worker calls this. */
#ifndef UNIT_TEST
static
#endif
esp_err_t radio_stop_sync(void)
{
    if (!g_radio_control_mtx) return ESP_ERR_INVALID_STATE;

    /* 7.5: unified flow regardless of prior state (STOPPING, FAULTED,
     * FAULTED_JOIN_PENDING, or a live RUNNING/BUFFERING session) — always
     * (re)request stop and attempt to join. This also implements 7.2's
     * "active session pointer remains set while JOIN_PENDING": only a
     * successful join ever clears s_active_session. */
    xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
    radio_session_t *s = s_active_session;
    if (!s) {
        g_radio_state = RADIO_STATE_STOPPED;
        xSemaphoreGive(g_radio_control_mtx);
        return ESP_OK;
    }
    g_radio_state = RADIO_STATE_STOPPING;
    atomic_store_explicit(&s->stop_requested, true, memory_order_release);
    /* 7.5: notify workers to wake from any interruptible wait. */
    if (s->stream_task) xTaskNotifyGive(s->stream_task);
    if (s->decoder_task) xTaskNotifyGive(s->decoder_task);
    xSemaphoreGive(g_radio_control_mtx);

    esp_err_t join_err = session_join(s, pdMS_TO_TICKS(RADIO_STOP_TIMEOUT_MS));

    if (join_err != ESP_OK) {
        xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
        g_radio_state = RADIO_STATE_FAULTED_JOIN_PENDING;
        xSemaphoreGive(g_radio_control_mtx);
        set_radio_error(RADIO_ERR_STOP_TIMEOUT, "worker join timeout");
        ESP_LOGW(TAG, "radio stop timeout (gen=%" PRIu32 "); session FAULTED_JOIN_PENDING", s->generation);
        printf("DIAG|RADIO|STOP_TIMEOUT|gen=%" PRIu32 "\n", s->generation);
        fflush(stdout);
        return join_err;
    }

    /* Joined — reclaim and destroy the session. */
    uint32_t gen = s->generation;
    xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
    if (s_active_session == s) s_active_session = NULL;
    g_radio_state = RADIO_STATE_STOPPED;
    xSemaphoreGive(g_radio_control_mtx);
    session_destroy_joined(s);

    ESP_LOGI(TAG, "radio stopped (gen=%" PRIu32 ")", gen);
    printf("DIAG|RADIO|STOPPED|gen=%" PRIu32 "\n", gen);
    fflush(stdout);
    return ESP_OK;
}

bool radio_is_playing(void)
{
    if (!g_radio_control_mtx) return false;
    xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
    bool playing = (g_radio_state == RADIO_STATE_RUNNING || g_radio_state == RADIO_STATE_BUFFERING);
    xSemaphoreGive(g_radio_control_mtx);
    return playing;
}

/* True only once the PCM cushion is primed — the arbiter feeds the radio to
 * I2S on this, not radio_is_playing(), so startup/rebuffer emit silence (or the
 * tone) rather than a starving ring. */
bool radio_audio_ready(void)
{
    /* RH-S3-13: coherent read of state + prebuffered flag.
     * Nested locks: g_radio_control_mtx -> g_radio_pcm_mtx. */
    if (!g_radio_control_mtx || !g_radio_pcm_mtx) return false;
    xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
    bool ready = false;
    if (g_radio_state == RADIO_STATE_RUNNING) {
        xSemaphoreTake(g_radio_pcm_mtx, portMAX_DELAY);
        ready = g_radio_prebuffered;
        xSemaphoreGive(g_radio_pcm_mtx);
    }
    xSemaphoreGive(g_radio_control_mtx);
    return ready;
}

radio_state_t radio_get_state(void)
{
    if (!g_radio_control_mtx) return RADIO_STATE_STOPPED;
    xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
    radio_state_t st = g_radio_state;
    xSemaphoreGive(g_radio_control_mtx);
    return st;
}

void radio_get_status(radio_status_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!g_radio_control_mtx) return;

    /* Single-mutex snapshot: acquire g_radio_control_mtx and copy all fields.
     * Telemetry/PCM fields are point-in-time snapshots — they are modified
     * by the worker tasks but reading them without g_radio_ring_mtx/g_radio_pcm_mtx is safe
     * for display purposes (single-word atomic reads on ESP32, strings are
     * copied consistently by strlcpy).
     * This matches bt_manager_get_status() pattern (Phase 7.10). */
    xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);

    /* Session state (protected by g_radio_control_mtx). */
    radio_state_t state = g_radio_state;
    out->generation = s_next_generation;
    out->state = state;
    out->playing = (state == RADIO_STATE_RUNNING ||
                    state == RADIO_STATE_STARTING ||
                    state == RADIO_STATE_BUFFERING);
    out->buffering = (state == RADIO_STATE_BUFFERING) ||
                      (state == RADIO_STATE_RUNNING && !g_radio_prebuffered);

    /* Telemetry snapshot (point-in-time, no additional lock needed). */
    out->codec = g_radio_codec;
    out->http_status = g_radio_http_status;
    out->bitrate_kbps = g_radio_bitrate;
    strlcpy(out->url, g_radio_url, sizeof(out->url));
    strlcpy(out->station, g_radio_station, sizeof(out->station));
    strlcpy(out->title, g_radio_title, sizeof(out->title));
    out->bytes_in = g_radio_bytes_in;
    out->ring_used = (uint32_t)g_radio_ring_count;
    out->ring_cap = (uint32_t)g_radio_ring_cap;
    out->reconnects = g_radio_reconnects;
    out->overflow_drops = g_radio_overflow;
    out->dec_rate = g_radio_dec_rate;
    out->dec_channels = g_radio_dec_ch;
    out->decode_errors = g_radio_decode_errors;
    out->last_error = g_radio_last_error;
    strlcpy(out->last_error_detail, g_radio_last_error_detail, sizeof(out->last_error_detail));

    /* PCM snapshot (point-in-time). */
    out->pcm_used = (uint32_t)g_radio_pcm_count;
    out->pcm_cap = (uint32_t)g_radio_pcm_cap;

    xSemaphoreGive(g_radio_control_mtx);

    /* Prebuffer setting is standalone (atomic, no lock needed). */
    out->prebuffer_ms = radio_get_prebuffer_ms();
}

int radio_get_prebuffer_ms(void)
{
    return (int)(atomic_load(&g_radio_prebuffer_bytes) / PCM_BYTES_PER_MS);
}

esp_err_t radio_set_prebuffer_ms(int ms)
{
    ms = (ms < PREBUF_MS_MIN) ? PREBUF_MS_MIN : ms;
    ms = (ms > PREBUF_MS_MAX) ? PREBUF_MS_MAX : ms;
    atomic_store(&g_radio_prebuffer_bytes, (size_t)ms * PCM_BYTES_PER_MS);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_RADIO, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_i32(h, NVS_KEY_PREBUF, ms);
        if (err == ESP_OK) err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "prebuffer applied but persistence failed: %s",
                 esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "prebuffer set to %d ms (%" PRIu32 " bytes)", ms, (uint32_t)atomic_load(&g_radio_prebuffer_bytes));
    return err;
}

/* 7.11: explicit compile-time default is stored FIRST, before any NVS
 * read, so a genuine load failure never leaves the threshold at whatever
 * it happened to already be. NOT_FOUND (missing namespace or key) is the
 * ordinary fresh-device case and returns ESP_OK with the default in
 * effect; any other error is a real load failure and is returned so the
 * caller can log/report it rather than silently treating it as success. */
static esp_err_t radio_prebuffer_load(void)
{
    atomic_store(&g_radio_prebuffer_bytes, (size_t)PREBUF_MS_DEFAULT * PCM_BYTES_PER_MS);

    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(NVS_NS_RADIO, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    int32_t ms = PREBUF_MS_DEFAULT;
    err = nvs_get_i32(h, NVS_KEY_PREBUF, &ms);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (ms < PREBUF_MS_MIN || ms > PREBUF_MS_MAX) return ESP_ERR_INVALID_SIZE;

    atomic_store(&g_radio_prebuffer_bytes, (size_t)ms * PCM_BYTES_PER_MS);
    return ESP_OK;
}
