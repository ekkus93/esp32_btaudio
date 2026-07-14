/*
 * radio device glue (RADIO-1b): fetch an HTTP(S) stream (playlist-resolved),
 * deinterleave ICY metadata, and fill a PSRAM ring the decoder drains.
 * Reconnect with backoff; telemetry. See radio.h.
 *
 * RH-S3-02: session-based lifecycle with monotonically increasing generation,
 * _Atomic stop_requested flag, event-group exit acknowledgement, and control
 * mutex for lifecycle transitions.
 */
#include "radio.h"
#include "radio_parse.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"

#include "radio_resampler.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_audio_dec_default.h"
#include "esp_aac_dec.h"

static const char *TAG = "radio";

/* ---- PSRAM byte ring (SPSC, mutex-guarded) ---- */
static uint8_t          *s_ring;
static size_t            s_cap, s_head, s_tail, s_count;
static SemaphoreHandle_t s_mtx;

static size_t ring_write(const uint8_t *d, size_t n)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    size_t w = (n < s_cap - s_count) ? n : s_cap - s_count;
    size_t first = s_cap - s_head;
    if (first > w) first = w;
    memcpy(s_ring + s_head, d, first);
    if (w > first) memcpy(s_ring, d + first, w - first);
    s_head = (s_head + w) % s_cap;
    s_count += w;
    xSemaphoreGive(s_mtx);
    return w;
}

size_t radio_read(uint8_t *dst, size_t len)
{
    if (!s_ring || !dst) return 0;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    size_t r = (len < s_count) ? len : s_count;
    size_t first = s_cap - s_tail;
    if (first > r) first = r;
    memcpy(dst, s_ring + s_tail, first);
    if (r > first) memcpy(dst + first, s_ring, r - first);
    s_tail = (s_tail + r) % s_cap;
    s_count -= r;
    xSemaphoreGive(s_mtx);
    return r;
}

/* ---- decoded-PCM ring (44.1 kHz stereo s16), producer=decoder, consumer=I2S ----
 * 4 bytes/frame @ 44100 Hz: 1 MiB ~= 5.9 s of decoded audio — a deep jitter
 * buffer so a multi-second TCP/WiFi stall drains the cushion instead of the
 * output. Playback is gated (s_prebuffered) until the ring first fills to
 * PCM_PREBUFFER_BYTES (~3 s), and re-gated if it ever fully drains, so recovery
 * re-buffers cleanly rather than restarting choppy. */
#define PCM_RING_BYTES      (1024 * 1024)
/* Prebuffer (jitter cushion) is runtime-adjustable via the web UI, persisted in
 * NVS. Bounded below the PCM ring so the cushion always fits. */
#define PCM_BYTES_PER_MS    176            /* 44100 Hz * 2ch * 2B / 1000 (rounded) */
#define PREBUF_MS_MIN       500
#define PREBUF_MS_MAX       5000           /* < PCM_RING_BYTES (~5.9 s) */
#define PREBUF_MS_DEFAULT   3000           /* ~3.0 s cushion before/again after dry */
#define NVS_NS_RADIO        "radio"
#define NVS_KEY_PREBUF      "prebuf_ms"

/* Timeout for radio_stop() — the workers must exit by this or the session is
 * FAULTED and restart is blocked. */
#define RADIO_STOP_TIMEOUT_MS 8000

static volatile size_t   s_prebuffer_bytes = (size_t)PREBUF_MS_DEFAULT * PCM_BYTES_PER_MS;
static void radio_prebuffer_load(void);
static uint8_t          *s_pcm;
static size_t            s_pcm_cap, s_pcm_head, s_pcm_tail, s_pcm_count;
static SemaphoreHandle_t s_pcm_mtx;
static volatile bool     s_prebuffered;    /* PCM cushion reached -> ok to feed I2S */

static size_t pcm_write(const uint8_t *d, size_t n)
{
    xSemaphoreTake(s_pcm_mtx, portMAX_DELAY);
    size_t w = (n < s_pcm_cap - s_pcm_count) ? n : s_pcm_cap - s_pcm_count;
    size_t first = s_pcm_cap - s_pcm_head;
    if (first > w) first = w;
    memcpy(s_pcm + s_pcm_head, d, first);
    if (w > first) memcpy(s_pcm, d + first, w - first);
    s_pcm_head = (s_pcm_head + w) % s_pcm_cap;
    s_pcm_count += w;
    if (!s_prebuffered && s_pcm_count >= s_prebuffer_bytes) s_prebuffered = true;
    xSemaphoreGive(s_pcm_mtx);
    return w;
}

size_t radio_pcm_read(int16_t *dst, size_t frames)
{
    if (!s_pcm || !dst) return 0;
    size_t want = frames * 4;   /* stereo s16 = 4 bytes/frame */
    xSemaphoreTake(s_pcm_mtx, portMAX_DELAY);
    size_t r = (want < s_pcm_count) ? want : s_pcm_count;
    r &= ~(size_t)3;            /* whole frames only */
    size_t first = s_pcm_cap - s_pcm_tail;
    if (first > r) first = r;
    memcpy(dst, s_pcm + s_pcm_tail, first);
    if (r > first) memcpy((uint8_t *)dst + first, s_pcm, r - first);
    s_pcm_tail = (s_pcm_tail + r) % s_pcm_cap;
    s_pcm_count -= r;
    /* Fully drained -> re-arm the prebuffer gate so the arbiter falls back to
     * silence until the cushion rebuilds, instead of feeding a starving ring. */
    if (s_prebuffered && s_pcm_count == 0) s_prebuffered = false;
    xSemaphoreGive(s_pcm_mtx);
    return r / 4;
}

/* ---- Session-based lifecycle (RH-S3-02) ---- */

/* Event bits for worker exit acknowledgement. */
#define RADIO_EVT_STREAM_EXITED  BIT0
#define RADIO_EVT_DECODER_EXITED BIT1
#define RADIO_EVT_ALL_EXITED \
    (RADIO_EVT_STREAM_EXITED | RADIO_EVT_DECODER_EXITED)

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

/* Module state protected by s_control_mtx. */
static SemaphoreHandle_t s_control_mtx;
static radio_session_t *s_active_session;
static radio_state_t s_radio_state;
static uint32_t s_next_generation;

/* ---- Telemetry (protected by s_mtx) ---- */
static char           s_url[RADIO_URL_MAX];
static char           s_station[RADIO_NAME_MAX];
static char           s_title[RADIO_TITLE_MAX];
static radio_codec_t  s_codec;
static int            s_bitrate, s_http_status;
static uint64_t       s_bytes_in;
static uint32_t       s_reconnects, s_overflow;

/* decoder telemetry */
static int            s_dec_rate, s_dec_ch;
static uint32_t       s_decode_errors;

/* Last error tracking */
static radio_err_t    s_last_error;
static char           s_last_error_detail[64];

/* Set the last error for the current session status. */
static void set_radio_error(radio_err_t err, const char *detail)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_last_error = err;
    s_last_error_detail[0] = '\0';
    if (detail) strlcpy(s_last_error_detail, detail, sizeof(s_last_error_detail));
    xSemaphoreGive(s_mtx);
}

/* ---- ICY demux callbacks ---- */
static void on_audio(void *ctx, const unsigned char *d, size_t n)
{
    (void)ctx;
    size_t w = ring_write(d, n);
    s_bytes_in += n;
    if (w < n) s_overflow += (uint32_t)(n - w);
}

static void on_title(void *ctx, const char *t)
{
    (void)ctx;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    strlcpy(s_title, t, sizeof(s_title));
    xSemaphoreGive(s_mtx);
    ESP_LOGI(TAG, "now playing: %s", t);
    printf("DIAG|RADIO|TITLE|%s\n", t);
    fflush(stdout);
}

/* ---- HTTP header capture ---- */

/* Backward-compat globals used by the event handler. */
static int s_hdr_metaint;
static int s_hdr_br;
static char s_hdr_ct[80];
static char s_hdr_name[RADIO_NAME_MAX];

static esp_err_t http_evt(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_HEADER) {
        if (strcasecmp(e->header_key, "icy-metaint") == 0) s_hdr_metaint = atoi(e->header_value);
        else if (strcasecmp(e->header_key, "content-type") == 0) strlcpy(s_hdr_ct, e->header_value, sizeof(s_hdr_ct));
        else if (strcasecmp(e->header_key, "icy-br") == 0) s_hdr_br = atoi(e->header_value);
        else if (strcasecmp(e->header_key, "icy-name") == 0) strlcpy(s_hdr_name, e->header_value, sizeof(s_hdr_name));
    }
    return ESP_OK;
}

static bool ci_contains(const char *hay, const char *needle)
{
    return hay && needle && strcasestr(hay, needle) != NULL;
}

static radio_codec_t codec_from_ct(const char *ct)
{
    if (ci_contains(ct, "mpeg") || ci_contains(ct, "mp3")) return RADIO_CODEC_MP3;
    if (ci_contains(ct, "aac") || ci_contains(ct, "mp4")) return RADIO_CODEC_AAC;
    return RADIO_CODEC_UNKNOWN;
}

/* Fetch a (small) playlist body and resolve to a stream URL; pass through a
 * direct stream URL unchanged. Best-effort: on resolve failure, use as-is. */
static void resolve_url(const char *in, char *out, size_t out_sz)
{
    if (!ci_contains(in, ".pls") && !ci_contains(in, ".m3u") && !ci_contains(in, "playlist")) {
        strlcpy(out, in, out_sz);
        return;
    }
    static char body[2048];
    int blen = 0;
    esp_http_client_config_t cfg = {
        .url = in, .timeout_ms = 8000, .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "esp-i2s-source/1",
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (esp_http_client_open(c, 0) == ESP_OK) {
        esp_http_client_fetch_headers(c);
        int r;
        while (blen < (int)sizeof(body) - 1 &&
               (r = esp_http_client_read(c, body + blen, sizeof(body) - 1 - blen)) > 0) {
            blen += r;
        }
        body[blen] = '\0';
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    if (blen > 0 && radio_playlist_first_url(body, out, out_sz)) {
        ESP_LOGI(TAG, "resolved playlist -> %s", out);
    } else {
        strlcpy(out, in, out_sz);
    }
}

/* ---- Decoder helpers ---- */

static esp_audio_simple_dec_handle_t open_decoder(radio_codec_t codec)
{
    esp_audio_simple_dec_cfg_t cfg = {0};
    esp_aac_dec_cfg_t aac = { .aac_plus_enable = true };
    if (codec == RADIO_CODEC_AAC) {
        cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
        cfg.dec_cfg = &aac;
        cfg.cfg_size = sizeof(aac);
    } else {
        cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    }
    esp_audio_simple_dec_handle_t h = NULL;
    if (esp_audio_simple_dec_open(&cfg, &h) != ESP_AUDIO_ERR_OK) return NULL;
    return h;
}

/* ---- Stream task (network -> compressed ring) ---- */
static void stream_task(void *arg)
{
    radio_session_t *s = arg;
    int backoff = 500;
    while (session_should_run(s)) {
        s_hdr_metaint = 0; s_hdr_br = 0; s_hdr_ct[0] = '\0'; s_hdr_name[0] = '\0';
        esp_http_client_config_t cfg = {
            .url = s->url,
            .timeout_ms = 6000,
            .event_handler = http_evt,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .buffer_size = 2048,
            .user_agent = "esp-i2s-source/1",
        };
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        if (!c) {
            set_radio_error(RADIO_ERR_HTTP_CLIENT_ALLOC, "client alloc failed");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        esp_http_client_set_header(c, "Icy-MetaData", "1");

        if (esp_http_client_open(c, 0) != ESP_OK) {
            esp_http_client_cleanup(c);
            goto reconnect;
        }
        esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_http_status = status;
        s_codec = codec_from_ct(s_hdr_ct);
        s_bitrate = s_hdr_br;
        xSemaphoreGive(s_mtx);
        if (s_hdr_name[0]) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            strlcpy(s_station, s_hdr_name, sizeof(s_station));
            xSemaphoreGive(s_mtx);
        }
        ESP_LOGI(TAG, "connected: codec=%s ct=%s metaint=%d br=%d name=%s",
                 radio_codec_str(s_codec), s_hdr_ct, s_hdr_metaint, s_bitrate, s_station);
        printf("DIAG|RADIO|CONNECTED|codec=%s,metaint=%d,br=%d\n",
               radio_codec_str(s_codec), s_hdr_metaint, s_bitrate);
        fflush(stdout);
        backoff = 500;

        radio_icy_demux_t demux;
        radio_icy_demux_init(&demux, s_hdr_metaint);
        static uint8_t buf[2048];
        while (session_should_run(s)) {
            int r = esp_http_client_read(c, (char *)buf, sizeof(buf));
            if (r <= 0) break;
            radio_icy_demux_feed(&demux, buf, (size_t)r, on_audio, on_title, NULL);
        }
        esp_http_client_close(c);
        esp_http_client_cleanup(c);

    reconnect:
        if (session_should_run(s)) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            s_reconnects++;
            xSemaphoreGive(s_mtx);
            vTaskDelay(pdMS_TO_TICKS(backoff));
            backoff = (backoff < 8000) ? backoff * 2 : 8000;
        }
    }

    /* Signal exit. */
    xEventGroupSetBits(s->events, RADIO_EVT_STREAM_EXITED);
    s->stream_task = NULL;
    vTaskDelete(NULL);
}

/* ---- Decoder task (compressed ring -> decode -> resample -> PCM ring) ---- */
static void decoder_task(void *arg)
{
    radio_session_t *s = arg;
    esp_audio_simple_dec_handle_t dec = NULL;
    radio_codec_t opened = RADIO_CODEC_UNKNOWN;
    radio_resampler_t rs;
    bool rs_ready = false;
    /* RH-S3-05: inbuf doubles as accumulation buffer for unconsumed decoder tail.
     * pending tracks unconsumed bytes that must be preserved across iterations. */
    #define DECODER_INPUT_CAP 4096
    static uint8_t inbuf[DECODER_INPUT_CAP];
    static uint8_t pcmbuf[16384];   /* one decoded frame */
    static int16_t rsbuf[8192];     /* resampled stereo frames (4096 max) */
    size_t pending = 0;

    while (session_should_run(s)) {
        radio_codec_t codec;
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        codec = s_codec;
        xSemaphoreGive(s_mtx);

        if (codec == RADIO_CODEC_UNKNOWN) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        if (!dec || codec != opened) {
            if (dec) { esp_audio_simple_dec_close(dec); dec = NULL; }
            dec = open_decoder(codec);
            opened = codec;
            rs_ready = false;
            if (!dec) {
                xSemaphoreTake(s_mtx, portMAX_DELAY);
                s_decode_errors++;
                xSemaphoreGive(s_mtx);
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            ESP_LOGI(TAG, "decoder open: %s", radio_codec_str(codec));
        }

 /* RH-S3-05: read new compressed data after the pending tail. */
        if (pending < DECODER_INPUT_CAP) {
            size_t got = radio_read(inbuf + pending, DECODER_INPUT_CAP - pending);
            pending += got;
        }

        if (pending == 0) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

        esp_audio_simple_dec_raw_t raw = { .buffer = inbuf, .len = (uint32_t)pending };
        size_t consumed_total = 0;
        while (raw.len > 0 && session_should_run(s)) {
            esp_audio_simple_dec_out_t out = { .buffer = pcmbuf, .len = sizeof(pcmbuf) };
            esp_audio_err_t err = esp_audio_simple_dec_process(dec, &raw, &out);
            if (err == ESP_AUDIO_ERR_OK && out.decoded_size > 0) {
                esp_audio_simple_dec_info_t info;
                esp_audio_simple_dec_get_info(dec, &info);
                if (!rs_ready || (int)info.sample_rate != rs.src_rate || info.channel != rs.channels) {
                    radio_resampler_init(&rs, info.sample_rate, info.channel);
                    rs_ready = true;
                    xSemaphoreTake(s_mtx, portMAX_DELAY);
                    s_dec_rate = info.sample_rate;
                    s_dec_ch = info.channel;
                    xSemaphoreGive(s_mtx);
                }
                /* RH-S3-04: loop resampler until all input frames consumed. */
                size_t in_frames = out.decoded_size / (2 * (info.channel ? info.channel : 1));
                size_t channels = (info.channel == 1) ? 1 : 2;
                size_t offset = 0;
                while (offset < in_frames && session_should_run(s)) {
                    size_t used = 0;
                    size_t of = radio_resampler_run(&rs,
                                                    (const int16_t *)pcmbuf + channels * offset,
                                                    in_frames - offset,
                                                    rsbuf,
                                                    sizeof(rsbuf) / (2 * sizeof(int16_t)),
                                                    &used);
                    if (of) {
                        size_t bytes = of * 4;
                        size_t written = 0;
                        while (written < bytes && session_should_run(s)) {
                            size_t n = pcm_write((const uint8_t *)rsbuf + written, bytes - written);
                            if (n == 0) {
                                vTaskDelay(pdMS_TO_TICKS(5));
                                continue;
                            }
                            written += n;
                        }
                        if (written != bytes) break;  /* PCM ring full */
                    } else if (used == 0) {
                        break;  /* resampler stalled */
                    }
                    offset += used;
                }
            } else if (err != ESP_AUDIO_ERR_OK) {
                xSemaphoreTake(s_mtx, portMAX_DELAY);
                s_decode_errors++;
                xSemaphoreGive(s_mtx);
                if (raw.consumed == 0) raw.consumed = 1;   /* force resync progress */
            }
            if (raw.consumed == 0) break;   /* needs more input */
            consumed_total += raw.consumed;
            raw.buffer += raw.consumed;
            raw.len -= raw.consumed;
            raw.consumed = 0;
        }

        /* RH-S3-05: preserve unconsumed decoder tail in accumulation buffer. */
        if (consumed_total > 0) {
            pending -= consumed_total;
            memmove(inbuf, inbuf + consumed_total, pending);
        } else if (pending == DECODER_INPUT_CAP) {
            /* No progress and buffer full — resync by dropping one byte. */
            memmove(inbuf, inbuf + 1, (size_t)(pending - 1));
            pending--;
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            s_decode_errors++;
            xSemaphoreGive(s_mtx);
        }
    }

    if (dec) esp_audio_simple_dec_close(dec);
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_dec_rate = 0;
    xSemaphoreGive(s_mtx);
    s->decoder_task = NULL;
    xEventGroupSetBits(s->events, RADIO_EVT_DECODER_EXITED);
    vTaskDelete(NULL);
}

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
    if (s_control_mtx && s_active_session) {
        radio_stop();
    }

    if (s_control_mtx) {
        vSemaphoreDelete(s_control_mtx);
        s_control_mtx = NULL;
    }
    if (s_pcm_mtx) {
        vSemaphoreDelete(s_pcm_mtx);
        s_pcm_mtx = NULL;
    }
    if (s_mtx) {
        vSemaphoreDelete(s_mtx);
        s_mtx = NULL;
    }

    /* Free ring buffers. */
    if (s_pcm) {
        free(s_pcm);
        s_pcm = NULL;
        s_pcm_cap = 0;
        s_pcm_head = s_pcm_tail = s_pcm_count = 0;
    }
    if (s_ring) {
        free(s_ring);
        s_ring = NULL;
        s_cap = 0;
        s_head = s_tail = s_count = 0;
    }

    s_prebuffered = false;
    s_radio_state = RADIO_STATE_STOPPED;
    s_active_session = NULL;
}

esp_err_t radio_init(size_t ring_bytes)
{
    /* Check if already initialized — reject double-init. */
    if (s_ring || s_control_mtx) return ESP_ERR_INVALID_STATE;

    /* Control mutex. */
    s_control_mtx = xSemaphoreCreateMutex();
    if (!s_control_mtx) return ESP_ERR_NO_MEM;

    s_cap = ring_bytes;
    s_ring = heap_caps_malloc(s_cap, MALLOC_CAP_SPIRAM);
    if (!s_ring) s_ring = heap_caps_malloc(s_cap, MALLOC_CAP_DEFAULT);
    if (!s_ring) return ESP_ERR_NO_MEM;

    /* Decoded-PCM ring: deep jitter buffer (~5.9 s), gated by s_prebuffered. */
    s_pcm_cap = PCM_RING_BYTES;
    s_pcm = heap_caps_malloc(s_pcm_cap, MALLOC_CAP_SPIRAM);
    if (!s_pcm) s_pcm = heap_caps_malloc(s_pcm_cap, MALLOC_CAP_DEFAULT);
    if (!s_pcm) return ESP_ERR_NO_MEM;

    s_mtx = xSemaphoreCreateMutex();
    s_pcm_mtx = xSemaphoreCreateMutex();
    if (!s_mtx || !s_pcm_mtx) {
        /* Cleanup: free what we created. */
        if (s_pcm_mtx) vSemaphoreDelete(s_pcm_mtx);
        if (s_mtx) vSemaphoreDelete(s_mtx);
        s_pcm_mtx = NULL;
        s_mtx = NULL;
        free(s_pcm); s_pcm = NULL;
        free(s_ring); s_ring = NULL;
        return ESP_ERR_NO_MEM;
    }

    radio_prebuffer_load();                    /* restore persisted prebuffer depth */
    esp_audio_dec_register_default();          /* low-level MP3/AAC decoders */
    esp_audio_simple_dec_register_default();   /* simple/frame-parser wrappers */
    return ESP_OK;
}

esp_err_t radio_play(const char *playlist_or_url)
{
    if (!playlist_or_url || !playlist_or_url[0]) return ESP_ERR_INVALID_ARG;
    if (!s_ring || !s_pcm || !s_control_mtx) return ESP_ERR_INVALID_STATE;

    /* Stop any previous session. */
    esp_err_t err = radio_stop();
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
    xSemaphoreTake(s_control_mtx, portMAX_DELAY);
    s_next_generation++;
    s->generation = s_next_generation;
    strlcpy(s->url, resolved, sizeof(s->url));
    s_radio_state = RADIO_STATE_STARTING;
    atomic_store_explicit(&s->stop_requested, false, memory_order_relaxed);
    xSemaphoreGive(s_control_mtx);

    /* Reset shared rings — safe because old session is gone (radio_stop() joined). */
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_head = s_tail = s_count = 0;
    strlcpy(s_url, resolved, sizeof(s_url));
    s_title[0] = s_station[0] = '\0';
    s_bytes_in = 0; s_overflow = 0; s_reconnects = 0;
    s_codec = RADIO_CODEC_UNKNOWN; s_bitrate = 0; s_http_status = 0;
    s_decode_errors = 0; s_dec_rate = 0; s_dec_ch = 0;
    s_last_error = RADIO_ERR_NONE;
    s_last_error_detail[0] = '\0';
    xSemaphoreGive(s_mtx);
    xSemaphoreTake(s_pcm_mtx, portMAX_DELAY);
    s_pcm_head = s_pcm_tail = s_pcm_count = 0;
    s_prebuffered = false;
    xSemaphoreGive(s_pcm_mtx);

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
    xSemaphoreTake(s_control_mtx, portMAX_DELAY);
    s_active_session = s;
    s_radio_state = RADIO_STATE_RUNNING;
    xSemaphoreGive(s_control_mtx);

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
    xSemaphoreTake(s_control_mtx, portMAX_DELAY);
    s_radio_state = RADIO_STATE_STOPPED;
    xSemaphoreGive(s_control_mtx);
    return ESP_ERR_NO_MEM;
}

esp_err_t radio_stop(void)
{
    if (!s_control_mtx) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_control_mtx, portMAX_DELAY);
    radio_session_t *s = s_active_session;
    if (!s) {
        s_radio_state = RADIO_STATE_STOPPED;
        xSemaphoreGive(s_control_mtx);
        return ESP_OK;
    }
    s_radio_state = RADIO_STATE_STOPPING;
    atomic_store_explicit(&s->stop_requested, true, memory_order_release);
    xSemaphoreGive(s_control_mtx);

    /* Wait for both workers to exit. */
    EventBits_t bits = xEventGroupWaitBits(
        s->events, RADIO_EVT_ALL_EXITED, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(RADIO_STOP_TIMEOUT_MS));

    if ((bits & RADIO_EVT_ALL_EXITED) != RADIO_EVT_ALL_EXITED) {
        /* Timeout — leave session registered, block restart. */
        xSemaphoreTake(s_control_mtx, portMAX_DELAY);
        s_radio_state = RADIO_STATE_FAULTED;
        xSemaphoreGive(s_control_mtx);
        ESP_LOGW(TAG, "radio stop timeout (gen=%" PRIu32 "); session FAULTED", s->generation);
        printf("DIAG|RADIO|STOP_TIMEOUT|gen=%" PRIu32 "\n", s->generation);
        fflush(stdout);
        return ESP_ERR_TIMEOUT;
    }

    /* Both exited — reclaim session. */
    xSemaphoreTake(s_control_mtx, portMAX_DELAY);
    if (s_active_session == s) s_active_session = NULL;
    s_radio_state = RADIO_STATE_STOPPED;
    xSemaphoreGive(s_control_mtx);

    vEventGroupDelete(s->events);
    free(s);

    ESP_LOGI(TAG, "radio stopped (gen=%" PRIu32 ")", s->generation);
    printf("DIAG|RADIO|STOPPED|gen=%" PRIu32 "\n", s->generation);
    fflush(stdout);
    return ESP_OK;
}

bool radio_is_playing(void)
{
    return s_radio_state == RADIO_STATE_RUNNING;
}

/* True only once the PCM cushion is primed — the arbiter feeds the radio to
 * I2S on this, not radio_is_playing(), so startup/rebuffer emit silence (or the
 * tone) rather than a starving ring. */
bool radio_audio_ready(void)
{
    return s_radio_state == RADIO_STATE_RUNNING && s_prebuffered;
}

radio_state_t radio_get_state(void)
{
    if (!s_control_mtx) return RADIO_STATE_STOPPED;
    xSemaphoreTake(s_control_mtx, portMAX_DELAY);
    radio_state_t st = s_radio_state;
    xSemaphoreGive(s_control_mtx);
    return st;
}

static void radio_prebuffer_load(void);

void radio_get_status(radio_status_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!s_control_mtx) return;

    /* Take telemetry lock first, then control lock (RH-S3-13 ordering rule). */
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    out->playing = (s_radio_state == RADIO_STATE_RUNNING ||
                    s_radio_state == RADIO_STATE_STARTING);
    out->buffering = s_radio_state == RADIO_STATE_RUNNING && !s_prebuffered;
    out->codec = s_codec;
    out->http_status = s_http_status;
    out->bitrate_kbps = s_bitrate;
    strlcpy(out->url, s_url, sizeof(out->url));
    strlcpy(out->station, s_station, sizeof(out->station));
    strlcpy(out->title, s_title, sizeof(out->title));
    out->bytes_in = s_bytes_in;
    out->ring_used = (uint32_t)s_count;
    out->ring_cap = (uint32_t)s_cap;
    out->reconnects = s_reconnects;
    out->overflow_drops = s_overflow;
    out->dec_rate = s_dec_rate;
    out->dec_channels = s_dec_ch;
    out->decode_errors = s_decode_errors;
    out->last_error = s_last_error;
    strlcpy(out->last_error_detail, s_last_error_detail, sizeof(out->last_error_detail));
    xSemaphoreGive(s_mtx);

    /* Generation and state from control lock. */
    xSemaphoreTake(s_control_mtx, portMAX_DELAY);
    out->generation = s_next_generation;
    out->state = s_radio_state;
    xSemaphoreGive(s_control_mtx);

    /* PCM ring stats (separate lock). */
    if (s_pcm_mtx) {
        xSemaphoreTake(s_pcm_mtx, portMAX_DELAY);
        out->pcm_used = (uint32_t)s_pcm_count;
        out->pcm_cap = (uint32_t)s_pcm_cap;
        xSemaphoreGive(s_pcm_mtx);
    }
    out->prebuffer_ms = radio_get_prebuffer_ms();
}

int radio_get_prebuffer_ms(void)
{
    return (int)(s_prebuffer_bytes / PCM_BYTES_PER_MS);
}

void radio_set_prebuffer_ms(int ms)
{
    ms = (ms < PREBUF_MS_MIN) ? PREBUF_MS_MIN : ms;
    ms = (ms > PREBUF_MS_MAX) ? PREBUF_MS_MAX : ms;
    s_prebuffer_bytes = (size_t)ms * PCM_BYTES_PER_MS;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_RADIO, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, NVS_KEY_PREBUF, ms);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "prebuffer set to %d ms (%" PRIu32 " bytes)", ms, (uint32_t)s_prebuffer_bytes);
}

static void radio_prebuffer_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_RADIO, NVS_READONLY, &h) != ESP_OK) return;
    int32_t ms = PREBUF_MS_DEFAULT;
    if (nvs_get_i32(h, NVS_KEY_PREBUF, &ms) == ESP_OK) {
        ms = (ms < PREBUF_MS_MIN) ? PREBUF_MS_MIN : ms;
        ms = (ms > PREBUF_MS_MAX) ? PREBUF_MS_MAX : ms;
        s_prebuffer_bytes = (size_t)ms * PCM_BYTES_PER_MS;
    }
    nvs_close(h);
}