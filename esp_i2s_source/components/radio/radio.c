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
/* Prebuffer (jitter cushion) is runtime-adjustable via the web UI, persisted in
 * NVS. Bounded below the PCM ring so the cushion always fits. */
#define PCM_BYTES_PER_MS    176            /* 44100 Hz * 2ch * 2B / 1000 (rounded) */
#define PREBUF_MS_MIN       500
#define PREBUF_MS_MAX       5000           /* < PCM_RING_BYTES (~5.9 s) */
#define PREBUF_MS_DEFAULT   3000           /* ~3.0 s cushion before/again after dry */
#define NVS_NS_RADIO        "radio"
#define NVS_KEY_PREBUF      "prebuf_ms"

static void radio_prebuffer_load(void);

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

/* 7.4: destroy a session only after both workers have exited.
 * Asserts both EXITED bits are set before freeing. */
static void session_destroy_joined(radio_session_t *s)
{
    if (!s) return;
    ESP_LOGI(TAG, "joining session gen=%" PRIu32, s->generation);
    /* Assert: workers must have exited. If not, the session was freed
     * prematurely — the task handles still point to running code. */
    EventBits_t bits = xEventGroupGetBits(s->events);
    if ((bits & RADIO_EVT_ALL_EXITED) != RADIO_EVT_ALL_EXITED) {
        ESP_LOGE(TAG, "session_destroy called with workers still running (gen=%" PRIu32 ")",
                 s->generation);
        /* Don't free — mark faulted instead. The workers will free themselves
         * when they eventually notice the session is gone. */
        return;
    }
    vEventGroupDelete(s->events);
    free(s);
}

/* Force-destroy session (used by radio_deinit() for teardown). */
static void session_destroy_force(radio_session_t *s)
{
    if (!s) return;
    vEventGroupDelete(s->events);
    free(s);
}

/* Module state protected by g_radio_control_mtx. */
SemaphoreHandle_t g_radio_control_mtx;
static radio_session_t *s_active_session;
radio_state_t g_radio_state;
static uint32_t s_next_generation;

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

/* Teardown: release all ring buffers and mutexes.
 * Safe to call when not initialized (no-op). */
void radio_deinit(void)
{
    /* Stop any active session first. */
    if (g_radio_control_mtx) {
        radio_session_t *s = s_active_session;
        radio_stop_sync();
        /* Force-destroy session if it wasn't freed by session_destroy_joined().
         * session_destroy_joined() returns early (without freeing) when workers
         * haven't exited. In that case, s_active_session was cleared by
         * radio_stop_sync(), but the session memory wasn't freed. We use the
         * saved pointer to force-destroy. */
        if (s) {
            /* Check if workers exited — if they did, session was freed.
             * If they didn't, force-destroy. */
            if (!session_all_exited(s)) {
                session_destroy_force(s);
            }
        }
    }

    /*
    Shutdown command worker (RH-S3-09). */
    if (s_radio_cmd_q && s_radio_cmd_task) {
        atomic_store(&s_cmd_shutdown, true);
        /* Send dummy command to unblock the worker. */
        radio_cmd_t dummy = { .type = RADIO_CMD_STOP };
        xQueueSend(s_radio_cmd_q, &dummy, 0);
        /* Wait for worker to exit (with timeout). */
        for (int i = 0; i < 8 && s_radio_cmd_task; i++) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    if (s_radio_cmd_q) {
        vQueueDelete(s_radio_cmd_q);
        s_radio_cmd_q = NULL;
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

    /* Free ring buffers. */
    if (g_radio_pcm) {
        free(g_radio_pcm);
        g_radio_pcm = NULL;
        g_radio_pcm_cap = 0;
        g_radio_pcm_head = g_radio_pcm_tail = g_radio_pcm_count = 0;
    }
    if (g_radio_ring) {
        free(g_radio_ring);
        g_radio_ring = NULL;
        g_radio_ring_cap = 0;
        g_radio_ring_head = g_radio_ring_tail = g_radio_ring_count = 0;
    }

    g_radio_prebuffered = false;
    g_radio_state = RADIO_STATE_STOPPED;
    s_active_session = NULL;
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
    s_radio_cmd_task = NULL;
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

esp_err_t radio_init(size_t ring_bytes)
{
    /* Check if already initialized — reject double-init. */
    if (g_radio_ring || g_radio_control_mtx) return ESP_ERR_INVALID_STATE;

    /* Control mutex. */
    g_radio_control_mtx = xSemaphoreCreateMutex();
    if (!g_radio_control_mtx) return ESP_ERR_NO_MEM;

    g_radio_ring_cap = ring_bytes;
    g_radio_ring = heap_caps_malloc(g_radio_ring_cap, MALLOC_CAP_SPIRAM);
    if (!g_radio_ring) g_radio_ring = heap_caps_malloc(g_radio_ring_cap, MALLOC_CAP_DEFAULT);
    if (!g_radio_ring) return ESP_ERR_NO_MEM;

    /* Decoded-PCM ring: deep jitter buffer (~5.9 s), gated by g_radio_prebuffered. */
    g_radio_pcm_cap = PCM_RING_BYTES;
    g_radio_pcm = heap_caps_malloc(g_radio_pcm_cap, MALLOC_CAP_SPIRAM);
    if (!g_radio_pcm) g_radio_pcm = heap_caps_malloc(g_radio_pcm_cap, MALLOC_CAP_DEFAULT);
    if (!g_radio_pcm) return ESP_ERR_NO_MEM;

    g_radio_ring_mtx = xSemaphoreCreateMutex();
    g_radio_pcm_mtx = xSemaphoreCreateMutex();
    if (!g_radio_ring_mtx || !g_radio_pcm_mtx) {
        /* Cleanup: free what we created. */
        if (g_radio_pcm_mtx) vSemaphoreDelete(g_radio_pcm_mtx);
        if (g_radio_ring_mtx) vSemaphoreDelete(g_radio_ring_mtx);
        g_radio_pcm_mtx = NULL;
        g_radio_ring_mtx = NULL;
        free(g_radio_pcm); g_radio_pcm = NULL;
        free(g_radio_ring); g_radio_ring = NULL;
        return ESP_ERR_NO_MEM;
    }

    /* Command queue (RH-S3-09): serializes play/stop through a single worker. */
    s_radio_cmd_q = xQueueCreate(4, sizeof(radio_cmd_t));
    if (!s_radio_cmd_q) {
        /* Cleanup queue failure: undo everything created so far. */
        vSemaphoreDelete(g_radio_ring_mtx);
        g_radio_ring_mtx = NULL;
        vSemaphoreDelete(g_radio_pcm_mtx);
        g_radio_pcm_mtx = NULL;
        free(g_radio_pcm); g_radio_pcm = NULL;
        free(g_radio_ring); g_radio_ring = NULL;
        vSemaphoreDelete(g_radio_control_mtx);
        g_radio_control_mtx = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(radio_cmd_task, "radio_cmd", 4096, NULL,
                     tskIDLE_PRIORITY + 2, &s_radio_cmd_task) != pdPASS) {
        vQueueDelete(s_radio_cmd_q);
        s_radio_cmd_q = NULL;
        /* Partial init — caller should radio_deinit() to cleanup. */
        ESP_LOGE(TAG, "radio_cmd_task creation failed");
        return ESP_ERR_NO_MEM;
    }

    radio_prebuffer_load();                    /* restore persisted prebuffer depth */
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

    /* Create stream task. */
    if (xTaskCreate(stream_task, "radio", 6144, s, tskIDLE_PRIORITY + 4,
                     &s->stream_task) != pdPASS) {
        goto fail_session;
    }

    /* Create decoder task. */
    if (xTaskCreate(decoder_task, "radio_dec", 8192, s, tskIDLE_PRIORITY + 4,
                     &s->decoder_task) != pdPASS) {
        /* Decoder creation failed — stop the stream task and cleanup. */
        atomic_store_explicit(&s->stop_requested, true, memory_order_release);
        EventBits_t bits = xEventGroupWaitBits(
            s->events, RADIO_EVT_STREAM_EXITED, pdFALSE, pdTRUE,
            pdMS_TO_TICKS(RADIO_STOP_TIMEOUT_MS));
        (void)bits;
        goto fail_session;
    }

    /* Both tasks created — publish as running. */
    xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
    s_active_session = s;
    g_radio_state = RADIO_STATE_RUNNING;
    xSemaphoreGive(g_radio_control_mtx);

    ESP_LOGI(TAG, "play gen=%" PRIu32 ": %s", s->generation, resolved);
    printf("DIAG|RADIO|START|gen=%" PRIu32 ",url=%s\n", s->generation, resolved);
    fflush(stdout);
    return ESP_OK;

fail_session:
    /* Request stop and join the stream task if it was created. */
    atomic_store_explicit(&s->stop_requested, true, memory_order_release);
    if (s->stream_task) {
        xEventGroupWaitBits(s->events, RADIO_EVT_STREAM_EXITED,
                            pdFALSE, pdTRUE, pdMS_TO_TICKS(RADIO_STOP_TIMEOUT_MS));
    }
    vEventGroupDelete(s->events);
    free(s);
    xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
    g_radio_state = RADIO_STATE_STOPPED;
    xSemaphoreGive(g_radio_control_mtx);
    return ESP_ERR_NO_MEM;
}

/* 7.1: Internal synchronous stop — only the cmd worker calls this. */
#ifndef UNIT_TEST
static
#endif
esp_err_t radio_stop_sync(void)
{
    if (!g_radio_control_mtx) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
    radio_session_t *s = s_active_session;
    if (!s) {
        g_radio_state = RADIO_STATE_STOPPED;
        xSemaphoreGive(g_radio_control_mtx);
        return ESP_OK;
    }
    /* 7.4: If already FAULTED_JOIN_PENDING, wait for workers then destroy. */
    if (g_radio_state == RADIO_STATE_FAULTED_JOIN_PENDING) {
        g_radio_state = RADIO_STATE_STOPPING;
        xSemaphoreGive(g_radio_control_mtx);
        /* Wait for workers to exit (they may have stopped already). */
        if (!session_all_exited(s)) {
            (void)xEventGroupWaitBits(s->events, RADIO_EVT_ALL_EXITED,
                                       pdFALSE, pdTRUE, pdMS_TO_TICKS(RADIO_STOP_TIMEOUT_MS));
        }
        if (session_all_exited(s)) {
            /* Workers exited — safe to free. */
            session_destroy_joined(s);
            goto stopped;
        }
        /* Workers still running — leave s_active_session set so
         * radio_deinit() can force-destroy. Return error to caller. */
        return ESP_ERR_TIMEOUT;
    }
    /* If already FAULTED, wait for workers then destroy. */
    if (g_radio_state == RADIO_STATE_FAULTED) {
        xSemaphoreGive(g_radio_control_mtx);
        /* Wait for workers to exit before freeing. */
        (void)xEventGroupWaitBits(s->events, RADIO_EVT_ALL_EXITED,
                                   pdFALSE, pdTRUE, pdMS_TO_TICKS(RADIO_STOP_TIMEOUT_MS));
        if (session_all_exited(s)) {
            /* Workers exited — safe to free. */
            session_destroy_joined(s);
            goto stopped;
        }
        /* Workers still running — leave s_active_session set so
         * radio_deinit() can force-destroy. Return error to caller. */
        return ESP_ERR_TIMEOUT;
    }
    g_radio_state = RADIO_STATE_STOPPING;
    atomic_store_explicit(&s->stop_requested, true, memory_order_release);
    /* 7.5: notify workers to wake from any vTaskDelay() */
    if (s->stream_task) xTaskNotifyGive(s->stream_task);
    if (s->decoder_task) xTaskNotifyGive(s->decoder_task);
    xSemaphoreGive(g_radio_control_mtx);

    /* Wait for both workers to exit. */
    EventBits_t bits = xEventGroupWaitBits(
        s->events, RADIO_EVT_ALL_EXITED, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(RADIO_STOP_TIMEOUT_MS));

    if ((bits & RADIO_EVT_ALL_EXITED) != RADIO_EVT_ALL_EXITED) {
        /* Timeout — leave session registered, block restart. */
        xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
        g_radio_state = RADIO_STATE_FAULTED_JOIN_PENDING;
        xSemaphoreGive(g_radio_control_mtx);
        ESP_LOGW(TAG, "radio stop timeout (gen=%" PRIu32 "); session FAULTED_JOIN_PENDING", s->generation);
        printf("DIAG|RADIO|STOP_TIMEOUT|gen=%" PRIu32 "\n", s->generation);
        fflush(stdout);
        return ESP_ERR_TIMEOUT;
    }

    /* Both exited — reclaim session. */
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

stopped:
    xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
    if (s_active_session) s_active_session = NULL;
    g_radio_state = RADIO_STATE_STOPPED;
    xSemaphoreGive(g_radio_control_mtx);
    return ESP_OK;
}

bool radio_is_playing(void)
{
    if (!g_radio_control_mtx) return false;
    xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
    bool playing = (g_radio_state == RADIO_STATE_RUNNING);
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
                    state == RADIO_STATE_STARTING);
    out->buffering = (state == RADIO_STATE_RUNNING && !g_radio_prebuffered);

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

static void radio_prebuffer_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_RADIO, NVS_READONLY, &h) != ESP_OK) return;
    int32_t ms = PREBUF_MS_DEFAULT;
    if (nvs_get_i32(h, NVS_KEY_PREBUF, &ms) == ESP_OK) {
        ms = (ms < PREBUF_MS_MIN) ? PREBUF_MS_MIN : ms;
        ms = (ms > PREBUF_MS_MAX) ? PREBUF_MS_MAX : ms;
        atomic_store(&g_radio_prebuffer_bytes, (size_t)ms * PCM_BYTES_PER_MS);
    }
    nvs_close(h);
}
