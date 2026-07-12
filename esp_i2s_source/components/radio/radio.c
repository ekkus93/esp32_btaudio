/*
 * radio device glue (RADIO-1b): fetch an HTTP(S) stream (playlist-resolved),
 * deinterleave ICY metadata, and fill a PSRAM ring the decoder drains.
 * Reconnect with backoff; telemetry. See radio.h.
 */
#include "radio.h"
#include "radio_parse.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

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

/* ---- state / telemetry ---- */
static TaskHandle_t   s_task;
static volatile bool  s_playing;
static char           s_url[RADIO_URL_MAX];
static char           s_station[RADIO_NAME_MAX];
static char           s_title[RADIO_TITLE_MAX];
static radio_codec_t  s_codec;
static int            s_bitrate, s_http_status;
static uint64_t       s_bytes_in;
static uint32_t       s_reconnects, s_overflow;

/* headers captured during fetch */
static volatile int s_hdr_metaint;
static int          s_hdr_br;
static char         s_hdr_ct[80];
static char         s_hdr_name[RADIO_NAME_MAX];

const char *radio_codec_str(radio_codec_t c)
{
    switch (c) {
    case RADIO_CODEC_MP3: return "mp3";
    case RADIO_CODEC_AAC: return "aac";
    default: return "unknown";
    }
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

static void stream_task(void *arg)
{
    (void)arg;
    int backoff = 500;
    while (s_playing) {
        s_hdr_metaint = 0; s_hdr_br = 0; s_hdr_ct[0] = '\0'; s_hdr_name[0] = '\0';
        esp_http_client_config_t cfg = {
            .url = s_url,
            .timeout_ms = 6000,
            .event_handler = http_evt,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .buffer_size = 2048,
            .user_agent = "esp-i2s-source/1",
        };
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        esp_http_client_set_header(c, "Icy-MetaData", "1");

        if (esp_http_client_open(c, 0) != ESP_OK) {
            esp_http_client_cleanup(c);
            goto reconnect;
        }
        esp_http_client_fetch_headers(c);
        s_http_status = esp_http_client_get_status_code(c);
        if (s_http_status != 200 && s_http_status != 0) {  /* 0 = ICY-style line */
            ESP_LOGW(TAG, "HTTP %d", s_http_status);
            esp_http_client_close(c);
            esp_http_client_cleanup(c);
            goto reconnect;
        }
        s_codec = codec_from_ct(s_hdr_ct);
        s_bitrate = s_hdr_br;
        if (s_hdr_name[0]) strlcpy(s_station, s_hdr_name, sizeof(s_station));
        ESP_LOGI(TAG, "connected: codec=%s ct=%s metaint=%d br=%d name=%s",
                 radio_codec_str(s_codec), s_hdr_ct, s_hdr_metaint, s_bitrate, s_station);
        printf("DIAG|RADIO|CONNECTED|codec=%s,metaint=%d,br=%d\n",
               radio_codec_str(s_codec), s_hdr_metaint, s_bitrate);
        fflush(stdout);
        backoff = 500;

        radio_icy_demux_t demux;
        radio_icy_demux_init(&demux, s_hdr_metaint);
        static uint8_t buf[2048];
        while (s_playing) {
            int r = esp_http_client_read(c, (char *)buf, sizeof(buf));
            if (r <= 0) break;
            radio_icy_demux_feed(&demux, buf, (size_t)r, on_audio, on_title, NULL);
        }
        esp_http_client_close(c);
        esp_http_client_cleanup(c);

    reconnect:
        if (s_playing) {
            s_reconnects++;
            vTaskDelay(pdMS_TO_TICKS(backoff));
            backoff = (backoff < 8000) ? backoff * 2 : 8000;
        }
    }
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t radio_init(size_t ring_bytes)
{
    s_cap = ring_bytes;
    s_ring = heap_caps_malloc(s_cap, MALLOC_CAP_SPIRAM);
    if (!s_ring) {
        s_ring = heap_caps_malloc(s_cap, MALLOC_CAP_DEFAULT);  /* fallback */
    }
    if (!s_ring) return ESP_ERR_NO_MEM;
    s_mtx = xSemaphoreCreateMutex();
    return s_mtx ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t radio_play(const char *playlist_or_url)
{
    if (!playlist_or_url || !playlist_or_url[0] || !s_ring) return ESP_ERR_INVALID_STATE;
    radio_stop();

    char resolved[RADIO_URL_MAX];
    resolve_url(playlist_or_url, resolved, sizeof(resolved));

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_head = s_tail = s_count = 0;
    strlcpy(s_url, resolved, sizeof(s_url));
    s_title[0] = s_station[0] = '\0';
    s_bytes_in = 0; s_overflow = 0; s_reconnects = 0;
    s_codec = RADIO_CODEC_UNKNOWN; s_bitrate = 0; s_http_status = 0;
    xSemaphoreGive(s_mtx);

    s_playing = true;
    if (xTaskCreate(stream_task, "radio", 6144, NULL, tskIDLE_PRIORITY + 4, &s_task) != pdPASS) {
        s_playing = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "play: %s", resolved);
    return ESP_OK;
}

void radio_stop(void)
{
    s_playing = false;
    for (int i = 0; i < 400 && s_task; i++) vTaskDelay(pdMS_TO_TICKS(20));  /* <=8s */
}

void radio_get_status(radio_status_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!s_mtx) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    out->playing = s_playing;
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
    xSemaphoreGive(s_mtx);
}
