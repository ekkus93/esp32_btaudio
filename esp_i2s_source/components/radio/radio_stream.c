/*
 * radio_stream.c — network domain: fetch an HTTP(S) stream (playlist-
 * resolved), deinterleave ICY metadata, and fill the compressed PSRAM ring
 * decoder_task() drains. Reconnect with backoff; telemetry. Split out of
 * radio.c (RADIO-1b); see radio.h.
 *
 * RH-S3-02: session-based lifecycle with monotonically increasing generation,
 * _Atomic stop_requested flag, event-group exit acknowledgement, and control
 * mutex for lifecycle transitions.
 */
#include "radio_internal.h"
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
#include "esp_log.h"

static const char *TAG = "radio";

/* Set the last error for the current session status. */
void set_radio_error(radio_err_t err, const char *detail)
{
    xSemaphoreTake(g_radio_ring_mtx, portMAX_DELAY);
    g_radio_last_error = err;
    g_radio_last_error_detail[0] = '\0';
    if (detail) strlcpy(g_radio_last_error_detail, detail, sizeof(g_radio_last_error_detail));
    xSemaphoreGive(g_radio_ring_mtx);
}

/* ---- ICY demux callbacks ---- */
static void on_audio(void *ctx, const unsigned char *d, size_t n)
{
    (void)ctx;
    size_t w = ring_write(d, n);
    g_radio_bytes_in += n;
    if (w < n) g_radio_overflow += (uint32_t)(n - w);
}

static void on_title(void *ctx, const char *t)
{
    (void)ctx;
    xSemaphoreTake(g_radio_ring_mtx, portMAX_DELAY);
    strlcpy(g_radio_title, t, sizeof(g_radio_title));
    xSemaphoreGive(g_radio_ring_mtx);
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
void resolve_url(const char *in, char *out, size_t out_sz)
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
    if (!c) {
        set_radio_error(RADIO_ERR_HTTP_CLIENT_ALLOC, "playlist client alloc failed");
        strlcpy(out, in, out_sz);
        return;
    }
    if (esp_http_client_open(c, 0) == ESP_OK) {
        esp_http_client_fetch_headers(c);
        int r;
        while (blen < (int)sizeof(body) - 1 &&
               (r = esp_http_client_read(c, body + blen, (int)(sizeof(body) - 1 - (size_t)blen))) > 0) {
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

/* ---- Stream task (network -> compressed ring) ---- */
void stream_task(void *arg)
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
            /* 7.5: interruptible wait on client alloc failure */
            xEventGroupWaitBits(s->events, RADIO_EVT_STREAM_EXITED | RADIO_EVT_STREAM_STARTED,
                                 pdFALSE, pdFALSE,
                                 pdMS_TO_TICKS(500 / portTICK_PERIOD_MS));
            continue;
        }
        esp_http_client_set_header(c, "Icy-MetaData", "1");

        if (esp_http_client_open(c, 0) != ESP_OK) {
            esp_http_client_cleanup(c);
            goto reconnect;
        }
        esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        xSemaphoreTake(g_radio_ring_mtx, portMAX_DELAY);
        g_radio_http_status = status;
        g_radio_codec = codec_from_ct(s_hdr_ct);
        g_radio_bitrate = s_hdr_br;
        xSemaphoreGive(g_radio_ring_mtx);
        if (s_hdr_name[0]) {
            xSemaphoreTake(g_radio_ring_mtx, portMAX_DELAY);
            strlcpy(g_radio_station, s_hdr_name, sizeof(g_radio_station));
            xSemaphoreGive(g_radio_ring_mtx);
        }

        /* 7.6: validate HTTP status — fault on non-2xx */
        if (status < 200 || status >= 300) {
            set_radio_error(RADIO_ERR_HTTP_STATUS, "bad HTTP status");
            ESP_LOGW(TAG, "HTTP status %d, faulting", status);
            esp_http_client_close(c);
            esp_http_client_cleanup(c);
            atomic_store_explicit(&s->stop_requested, true, memory_order_release);
            xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
            g_radio_state = RADIO_STATE_FAULTED;
            xSemaphoreGive(g_radio_control_mtx);
            /* 7.3: break to single exit — not continue */
            break;
        }

        /* RH-S3-21: terminate session on unsupported codec.
         * Reconnecting to the same URL won't produce a different codec,
         * so fault out rather than fill the compressed ring forever. */
        if (g_radio_codec == RADIO_CODEC_UNKNOWN) {
            set_radio_error(RADIO_ERR_UNSUPPORTED_CONTENT, s_hdr_ct);
            ESP_LOGW(TAG, "unsupported codec: %s", s_hdr_ct);
            esp_http_client_close(c);
            esp_http_client_cleanup(c);
            atomic_store_explicit(&s->stop_requested, true, memory_order_release);
            xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
            g_radio_state = RADIO_STATE_FAULTED;
            xSemaphoreGive(g_radio_control_mtx);
            /* 7.3: break to single exit — not continue */
            break;
        }

        ESP_LOGI(TAG, "connected: codec=%s ct=%s metaint=%d br=%d name=%s",
                 radio_codec_str(g_radio_codec), s_hdr_ct, s_hdr_metaint, g_radio_bitrate, g_radio_station);
        printf("DIAG|RADIO|CONNECTED|codec=%s,metaint=%d,br=%d\n",
               radio_codec_str(g_radio_codec), s_hdr_metaint, g_radio_bitrate);
        fflush(stdout);
        backoff = 500;

        /* 7.2: signal startup — stream has connected successfully */
        xEventGroupSetBits(s->events, RADIO_EVT_STREAM_STARTED);

        radio_icy_demux_t demux;
        radio_icy_demux_init(&demux, s_hdr_metaint);
        static uint8_t buf[2048];
        while (session_should_run(s)) {
            /* 7.7: backpressure — check ring free space before reading */
            size_t free_space;
            xSemaphoreTake(g_radio_ring_mtx, portMAX_DELAY);
            free_space = g_radio_ring_cap - g_radio_ring_count;
            xSemaphoreGive(g_radio_ring_mtx);
            if (free_space < (sizeof(buf))) {
                /* Ring full — drain wait (interruptible by stop) */
                if (wait_or_stop(s, 10)) break;
                continue;
            }
            int r = esp_http_client_read(c, (char *)buf, sizeof(buf));
            if (r <= 0) break;
            radio_icy_demux_feed(&demux, buf, (size_t)r, on_audio, on_title, NULL);
        }
        esp_http_client_close(c);
        esp_http_client_cleanup(c);

    reconnect:
        if (session_should_run(s)) {
            xSemaphoreTake(g_radio_ring_mtx, portMAX_DELAY);
            g_radio_reconnects++;
            xSemaphoreGive(g_radio_ring_mtx);
            /* 7.5: interruptible backoff wait via event group */
            xEventGroupWaitBits(s->events, RADIO_EVT_STREAM_EXITED | RADIO_EVT_STREAM_STARTED,
                                 pdFALSE, pdFALSE,
                                 pdMS_TO_TICKS((uint32_t)backoff / portTICK_PERIOD_MS));
            backoff = (backoff < 8000) ? backoff * 2 : 8000;
        }
    }

    /* 7.3: single exit label — signal exit and cleanup */
    xEventGroupSetBits(s->events, RADIO_EVT_STREAM_EXITED);
    s->stream_task = NULL;
    vTaskDelete(NULL);
}
