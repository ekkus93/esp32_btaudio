/*
 * radio_internal.h — private declarations shared across the radio.c split
 * (radio.c core, radio_ring.c, radio_stream.c, radio_decode.c). Not part of
 * the public API (that's radio.h); nothing outside this component's .c
 * files should include it.
 *
 * SPLIT_AND_REFRACT method (see esp_bt_audio_source's bt_source_mock.c /
 * bt_source_stubs.c split): file-scope state that's touched from more than
 * one domain is de-static'd, with its single definition kept in whichever
 * TU owns that domain and an `extern` declaration here for the others.
 */
#pragma once

#include "radio.h"

#include <stdatomic.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

/* Timeout for radio_stop() — the workers must exit by this or the session is
 * FAULTED and restart is blocked. */
#define RADIO_STOP_TIMEOUT_MS 8000

/* Timeout for radio_deinit() waiting on the command worker's exit
 * acknowledgement bit (7.10). */
#define RADIO_CMD_EXIT_TIMEOUT_MS 4000

/* Module-level event group (radio.c core): command-worker exit
 * acknowledgement (7.10), distinct from any session's per-play event group. */
#define RADIO_MODULE_EVT_CMD_EXITED ((EventBits_t)1)
extern EventGroupHandle_t g_radio_module_events;

/* Prebuffer (jitter cushion) defaults — shared so radio_ring.c can give
 * g_radio_prebuffer_bytes a correct compile-time default (7.11: a fresh
 * device with no NVS key must not silently prebuffer at 0 ms). */
#define PCM_BYTES_PER_MS    176            /* 44100 Hz * 2ch * 2B / 1000 (rounded) */
#define PREBUF_MS_MIN       500
#define PREBUF_MS_MAX       5000           /* < PCM_RING_BYTES (~5.9 s) */
#define PREBUF_MS_DEFAULT   3000           /* ~3.0 s cushion before/again after dry */

/* Session object — one per radio_play() invocation. Owns the stop flag, event
 * group, and task handles. Freed only after both workers have exited. */
typedef struct radio_session {
    uint32_t generation;
    char url[RADIO_URL_MAX];
    TaskHandle_t stream_task;
    TaskHandle_t decoder_task;
    EventGroupHandle_t events;
    _Atomic bool stop_requested;
} radio_session_t;

/* Helper: check if the session should continue running.
 * Returns true if the session exists and stop has not been requested. */
static inline bool session_should_run(const radio_session_t *s)
{
    return s && !atomic_load_explicit(&s->stop_requested, memory_order_acquire);
}

/* 7.4: check if both workers have exited for a session. */
static inline bool session_all_exited(const radio_session_t *s)
{
    if (!s || !s->events) return false;
    EventBits_t bits = xEventGroupGetBits(s->events);
    return (bits & RADIO_EVT_ALL_EXITED) == RADIO_EVT_ALL_EXITED;
}

/* 7.4: wait (bounded) for both workers to exit. Does not free anything —
 * callers decide what to do with the result (session_destroy_joined() on
 * ESP_OK, or attach as FAULTED_JOIN_PENDING and retain on ESP_ERR_TIMEOUT). */
static inline esp_err_t session_join(radio_session_t *s, TickType_t timeout)
{
    EventBits_t bits = xEventGroupWaitBits(
        s->events, RADIO_EVT_ALL_EXITED, pdFALSE, pdTRUE, timeout);
    return ((bits & RADIO_EVT_ALL_EXITED) == RADIO_EVT_ALL_EXITED) ? ESP_OK : ESP_ERR_TIMEOUT;
}

/* 7.5: interruptible wait — waits up to ms, returns true if stop requested.
   Workers call this instead of vTaskDelay(). radio_stop_sync() sends a
   task notification to wake them. */
static inline bool wait_or_stop(radio_session_t *s, uint32_t ms)
{
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms));
    return !session_should_run(s);
}

/* ---- compressed byte ring (radio_ring.c) ---- */
extern uint8_t          *g_radio_ring;
extern size_t             g_radio_ring_cap, g_radio_ring_head, g_radio_ring_tail, g_radio_ring_count;
extern SemaphoreHandle_t  g_radio_ring_mtx;
size_t ring_write(const uint8_t *d, size_t n);

/* ---- decoded-PCM ring (radio_ring.c) ---- */
extern uint8_t          *g_radio_pcm;
extern size_t             g_radio_pcm_cap, g_radio_pcm_head, g_radio_pcm_tail, g_radio_pcm_count;
extern SemaphoreHandle_t  g_radio_pcm_mtx;
extern volatile bool      g_radio_prebuffered;
extern atomic_size_t      g_radio_prebuffer_bytes;
size_t pcm_write(const uint8_t *d, size_t n);

/* ---- session control state (radio.c core) ---- */
extern SemaphoreHandle_t g_radio_control_mtx;
extern radio_state_t     g_radio_state;

/* ---- telemetry (radio.c core; guarded by g_radio_ring_mtx) ---- */
extern char          g_radio_url[RADIO_URL_MAX];
extern char          g_radio_station[RADIO_NAME_MAX];
extern char          g_radio_title[RADIO_TITLE_MAX];
extern radio_codec_t g_radio_codec;
extern int           g_radio_bitrate, g_radio_http_status;
extern uint64_t      g_radio_bytes_in;
extern uint32_t      g_radio_reconnects, g_radio_overflow;
extern int           g_radio_dec_rate, g_radio_dec_ch;
extern uint32_t      g_radio_decode_errors;
extern radio_err_t   g_radio_last_error;
extern char          g_radio_last_error_detail[64];

/* Set the last error for the current session status (radio_stream.c). */
void set_radio_error(radio_err_t err, const char *detail);

/* 7.8: only mutate g_radio_state on behalf of the generation that is still
 * s_active_session's — a stale worker from a session that has since been
 * stopped/replaced can't clobber a newer session's state (radio.c core). */
void radio_set_state_for_generation(uint32_t generation, radio_state_t state);

/* 7.8: transition BUFFERING -> RUNNING once both READY bits are set for the
 * given generation (radio.c core; called by stream_task/decoder_task right
 * after each sets its own READY bit). */
void radio_try_publish_running(radio_session_t *s);

/* Fetch a (small) playlist body and resolve to a stream URL; pass through a
 * direct stream URL unchanged. Best-effort: on resolve failure, use as-is.
 * (radio_stream.c; called by radio_play_sync() in radio.c core.) */
void resolve_url(const char *in, char *out, size_t out_sz);

/* Worker task entry points, started via xTaskCreate() from radio_play_sync()
 * in radio.c core. */
void stream_task(void *arg);    /* radio_stream.c: network -> compressed ring */
void decoder_task(void *arg);   /* radio_decode.c: compressed ring -> PCM ring */
