/*
 * radio_stream.c — network domain: fetch an HTTP(S) stream (playlist-
 * resolved), deinterleave ICY metadata, and fill the compressed PSRAM ring
 * decoder_task() drains. Reconnect with backoff; telemetry. Split out of
 * radio.c (RADIO-1b); see radio.h.
 *
 * RH-S3-02: session-based lifecycle with monotonically increasing generation,
 * _Atomic stop_requested flag, event-group exit acknowledgement, and control
 * mutex for lifecycle transitions.
 *
 * FIX3 Phase 8: bounded, stop-aware reconnect backoff (8.1); typed,
 * fail-closed playlist resolution (8.2); explicit redirect validation
 * against the same destination policy as the initial URL (8.3); permanent
 * vs. transient failure classification (8.4).
 */
#include "radio_internal.h"
#include "radio_parse.h"
#include "url_policy.h"

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
#include "esp_timer.h"

static const char *TAG = "radio";

/* 8.3: bound how many 3xx hops a single connection attempt will follow. */
#define MAX_REDIRECTS 5

/* 8.1: reconnect stability threshold (RESPONSES doc decision 10) — both
 * conditions must hold, checked once per connection, to reset the backoff
 * counter back to the start of the schedule. */
#define RECONNECT_STABLE_MS     10000
#define RECONNECT_STABLE_BYTES  32768u

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
static int  s_hdr_metaint;
static int  s_hdr_br;
static char s_hdr_ct[80];
static char s_hdr_name[RADIO_NAME_MAX];
static char s_hdr_location[RADIO_URL_MAX];

static esp_err_t http_evt(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_HEADER) {
        if (strcasecmp(e->header_key, "icy-metaint") == 0) s_hdr_metaint = atoi(e->header_value);
        else if (strcasecmp(e->header_key, "content-type") == 0) strlcpy(s_hdr_ct, e->header_value, sizeof(s_hdr_ct));
        else if (strcasecmp(e->header_key, "icy-br") == 0) s_hdr_br = atoi(e->header_value);
        else if (strcasecmp(e->header_key, "icy-name") == 0) strlcpy(s_hdr_name, e->header_value, sizeof(s_hdr_name));
        else if (strcasecmp(e->header_key, "location") == 0) strlcpy(s_hdr_location, e->header_value, sizeof(s_hdr_location));
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

/* ---- 8.2: typed, fail-closed input resolution ---- */

/* True if `url`'s path (before any '?' or '#') ends in .pls or .m3u,
 * case-insensitively — a query string like "?ref=playlist" must not match. */
static bool has_playlist_extension(const char *url)
{
    if (!url) return false;
    size_t len = strlen(url);
    const char *q = strchr(url, '?');
    const char *h = strchr(url, '#');
    size_t path_len = len;
    if (q && (size_t)(q - url) < path_len) path_len = (size_t)(q - url);
    if (h && (size_t)(h - url) < path_len) path_len = (size_t)(h - url);

    if (path_len >= 4 && strncasecmp(url + path_len - 4, ".pls", 4) == 0) return true;
    if (path_len >= 4 && strncasecmp(url + path_len - 4, ".m3u", 4) == 0) return true;
    return false;
}

#define PLAYLIST_BODY_MAX 8192

esp_err_t radio_resolve_input(const char *input, radio_resolution_t *out)
{
    if (!input || !input[0] || !out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    if (!has_playlist_extension(input)) {
        size_t len = strnlen(input, RADIO_URL_MAX);
        if (len == 0 || len >= RADIO_URL_MAX) {
            set_radio_error(RADIO_ERR_UNSUPPORTED_CONTENT, "url_too_long");
            return ESP_ERR_INVALID_SIZE;
        }
        if (!url_policy_check_literal(input)) {
            set_radio_error(RADIO_ERR_UNSUPPORTED_CONTENT, "url_blocked");
            return ESP_ERR_INVALID_ARG;
        }
        out->kind = RADIO_INPUT_DIRECT;
        memcpy(out->resolved_url, input, len + 1);
        return ESP_OK;
    }

    out->kind = RADIO_INPUT_PLAYLIST;

    esp_http_client_config_t cfg = {
        .url = input, .timeout_ms = 8000, .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "esp-i2s-source/1",
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        set_radio_error(RADIO_ERR_HTTP_CLIENT_ALLOC, "playlist_fetch");
        return ESP_ERR_NO_MEM;
    }
    if (esp_http_client_open(c, 0) != ESP_OK) {
        esp_http_client_cleanup(c);
        set_radio_error(RADIO_ERR_NETWORK_OPEN_FAILED, "playlist_fetch");
        return ESP_FAIL;
    }
    esp_http_client_fetch_headers(c);

    static char body[PLAYLIST_BODY_MAX + 1];
    int blen = 0, r;
    while (blen < PLAYLIST_BODY_MAX &&
           (r = esp_http_client_read(c, body + blen, (int)(PLAYLIST_BODY_MAX - blen))) > 0) {
        blen += r;
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);

    if (blen <= 0) {
        set_radio_error(RADIO_ERR_UNSUPPORTED_CONTENT, "playlist_empty");
        return ESP_ERR_NOT_FOUND;
    }
    if (blen >= PLAYLIST_BODY_MAX) {
        /* Filled the cap — treat as oversized rather than silently
         * truncating and parsing a partial playlist. */
        set_radio_error(RADIO_ERR_UNSUPPORTED_CONTENT, "playlist_oversized");
        return ESP_ERR_INVALID_SIZE;
    }
    body[blen] = '\0';

    char resolved[RADIO_URL_MAX];
    if (!radio_playlist_first_url(body, resolved, sizeof(resolved))) {
        set_radio_error(RADIO_ERR_UNSUPPORTED_CONTENT, "playlist_parse");
        return ESP_ERR_INVALID_ARG;
    }
    if (!url_policy_check_literal(resolved)) {
        set_radio_error(RADIO_ERR_UNSUPPORTED_CONTENT, "playlist_url_blocked");
        return ESP_ERR_INVALID_ARG;
    }

    strlcpy(out->resolved_url, resolved, sizeof(out->resolved_url));
    ESP_LOGI(TAG, "resolved playlist -> %s", out->resolved_url);
    return ESP_OK;
}

/* ---- 8.3: redirect target resolution + destination-policy re-check ---- */

/* Resolve a Location header against the URL it was received for. Absolute
 * ("scheme://...") and root-relative ("/path") locations are supported —
 * stream-server redirects are overwhelmingly one or the other in practice.
 * Any other relative form is rejected rather than guessed at. */
static bool resolve_redirect_location(const char *base_url, const char *location,
                                       char *out, size_t out_sz)
{
    if (!location || !location[0]) return false;

    if (strstr(location, "://")) {
        size_t len = strnlen(location, out_sz);
        if (len == 0 || len >= out_sz) return false;
        memcpy(out, location, len + 1);
        return true;
    }

    if (location[0] == '/') {
        const char *p = strstr(base_url, "://");
        if (!p) return false;
        p += 3;
        const char *host_end = p;
        while (*host_end && *host_end != '/' && *host_end != '?' && *host_end != '#') host_end++;
        size_t prefix_len = (size_t)(host_end - base_url);
        size_t loc_len = strlen(location);
        if (prefix_len + loc_len >= out_sz) return false;
        memcpy(out, base_url, prefix_len);
        memcpy(out + prefix_len, location, loc_len + 1);
        return true;
    }

    return false;
}

/* Literal-IP policy on both host and device; DNS-time policy (device-only —
 * needs lwip's getaddrinfo, wired here per the FIX3 5B deferral note). */
static bool redirect_target_allowed(const char *url)
{
    if (!url_policy_check_literal(url)) return false;
#ifdef ESP_PLATFORM
    char host[256];
    if (url_policy_extract_host(url, host, sizeof(host))) {
        if (url_policy_resolve_and_check(host, "80") != ESP_OK) return false;
    }
#endif
    return true;
}

/* ---- 8.1: bounded, stop-aware reconnect backoff ---- */

static uint32_t reconnect_delay_ms(uint32_t attempt)
{
    static const uint32_t schedule[] = {500, 1000, 2000, 4000, 8000, 15000};
    size_t idx = attempt < (sizeof(schedule) / sizeof(schedule[0]))
        ? attempt : (sizeof(schedule) / sizeof(schedule[0]) - 1u);
    return schedule[idx];
}

/* ---- 8.3: connect, following redirects against the same policy ---- */

/* Attempts one connection to `url`, following up to MAX_REDIRECTS same-
 * policy-checked redirects. On success, returns an open client positioned
 * at the terminal (non-3xx) response — caller reads status/headers as
 * usual. On failure, returns NULL; *out_permanent is true if the caller
 * should fault the session outright (redirect policy/limit violation)
 * rather than reconnect with backoff (transient connect failure). */
static esp_http_client_handle_t connect_with_redirects(const char *url, bool *out_permanent)
{
    char current_url[RADIO_URL_MAX];
    strlcpy(current_url, url, sizeof(current_url));
    *out_permanent = false;

    for (int hop = 0; hop <= MAX_REDIRECTS; hop++) {
        s_hdr_metaint = 0; s_hdr_br = 0; s_hdr_ct[0] = '\0';
        s_hdr_name[0] = '\0'; s_hdr_location[0] = '\0';

        esp_http_client_config_t cfg = {
            .url = current_url,
            .timeout_ms = 6000,
            .event_handler = http_evt,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .buffer_size = 2048,
            .user_agent = "esp-i2s-source/1",
            .disable_auto_redirect = true,
        };
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        if (!c) {
            set_radio_error(RADIO_ERR_HTTP_CLIENT_ALLOC, "client alloc failed");
            return NULL;
        }
        esp_http_client_set_header(c, "Icy-MetaData", "1");

        if (esp_http_client_open(c, 0) != ESP_OK) {
            esp_http_client_cleanup(c);
            return NULL;
        }
        esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);

        if (status < 300 || status >= 400) {
            return c;   /* terminal (2xx/4xx/5xx) — caller classifies it */
        }

        /* 3xx: validate and follow, or reject. */
        esp_http_client_close(c);
        esp_http_client_cleanup(c);

        if (hop == MAX_REDIRECTS) {
            set_radio_error(RADIO_ERR_HTTP_STATUS, "redirect_limit");
            *out_permanent = true;
            return NULL;
        }
        char next_url[RADIO_URL_MAX];
        if (!resolve_redirect_location(current_url, s_hdr_location, next_url, sizeof(next_url))) {
            set_radio_error(RADIO_ERR_HTTP_STATUS, "redirect_malformed");
            *out_permanent = true;
            return NULL;
        }
        if (!redirect_target_allowed(next_url)) {
            set_radio_error(RADIO_ERR_HTTP_STATUS, "redirect_url_blocked");
            *out_permanent = true;
            return NULL;
        }
        strlcpy(current_url, next_url, sizeof(current_url));
    }
    return NULL;
}

/* ---- Stream task (network -> compressed ring) ---- */
void stream_task(void *arg)
{
    radio_session_t *s = arg;
    uint32_t attempt = 0;
    bool     awaiting_stable = false;
    int64_t  connected_at_us = 0;
    uint64_t bytes_in_at_connect = 0;

    /* 7.3: ENTERED fires immediately — before any operational check. */
    xEventGroupSetBits(s->events, RADIO_EVT_STREAM_ENTERED);

    while (session_should_run(s)) {
        bool permanent_fault = false;
        esp_http_client_handle_t c = connect_with_redirects(s->url, &permanent_fault);
        if (!c) {
            if (permanent_fault) {
                /* 8.4/8.9: redirect policy/limit violations are permanent —
                 * generation-safe fault, no further reconnect attempts. */
                radio_session_fault(s, RADIO_ERR_HTTP_STATUS, "redirect rejected");
                break;
            }
            /* 8.4: transient connect failure — reconnect with backoff. */
            goto reconnect;
        }

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

        /* 8.4: classify — 5xx and 429 are transient (reconnect with
         * backoff); any other non-2xx is permanent (fault, no retry). */
        if (status >= 500 && status < 600) {
            set_radio_error(RADIO_ERR_HTTP_STATUS, "5xx transient");
            ESP_LOGW(TAG, "HTTP %d (transient), reconnecting", status);
            esp_http_client_close(c);
            esp_http_client_cleanup(c);
            goto reconnect;
        }
        if (status == 429) {
            set_radio_error(RADIO_ERR_HTTP_STATUS, "429 retryable");
            esp_http_client_close(c);
            esp_http_client_cleanup(c);
            goto reconnect;
        }
        if (status < 200 || status >= 300) {
            set_radio_error(RADIO_ERR_HTTP_STATUS, "bad HTTP status");
            ESP_LOGW(TAG, "HTTP status %d (permanent), faulting", status);
            esp_http_client_close(c);
            esp_http_client_cleanup(c);
            radio_session_fault(s, RADIO_ERR_HTTP_STATUS, "bad HTTP status");
            break;
        }

        /* RH-S3-21: terminate session on unsupported codec — permanent;
         * reconnecting to the same URL won't produce a different codec. */
        if (g_radio_codec == RADIO_CODEC_UNKNOWN) {
            set_radio_error(RADIO_ERR_UNSUPPORTED_CONTENT, s_hdr_ct);
            ESP_LOGW(TAG, "unsupported codec: %s", s_hdr_ct);
            esp_http_client_close(c);
            esp_http_client_cleanup(c);
            radio_session_fault(s, RADIO_ERR_UNSUPPORTED_CONTENT, s_hdr_ct);
            break;
        }

        ESP_LOGI(TAG, "connected: codec=%s ct=%s metaint=%d br=%d name=%s",
                 radio_codec_str(g_radio_codec), s_hdr_ct, s_hdr_metaint, g_radio_bitrate, g_radio_station);
        printf("DIAG|RADIO|CONNECTED|codec=%s,metaint=%d,br=%d\n",
               radio_codec_str(g_radio_codec), s_hdr_metaint, g_radio_bitrate);
        fflush(stdout);

        /* 8.1: start this connection's stability window. */
        connected_at_us = esp_timer_get_time();
        bytes_in_at_connect = g_radio_bytes_in;
        awaiting_stable = (attempt > 0);

        /* 7.3/7.8: READY — stream has connected and passed its operational
         * checks (status + codec). Try to promote BUFFERING -> RUNNING now
         * that this worker is ready (the decoder may already be READY too). */
        xEventGroupSetBits(s->events, RADIO_EVT_STREAM_READY);
        radio_try_publish_running(s);

        radio_icy_demux_t demux;
        radio_icy_demux_init(&demux, s_hdr_metaint);
        static uint8_t buf[2048];
        while (session_should_run(s)) {
            /* 8.1: once this connection has been stable for both the
             * required duration and payload volume, reset the backoff
             * schedule exactly once. */
            if (awaiting_stable) {
                int64_t elapsed_ms = (esp_timer_get_time() - connected_at_us) / 1000;
                uint64_t bytes_since = g_radio_bytes_in - bytes_in_at_connect;
                if (elapsed_ms >= RECONNECT_STABLE_MS && bytes_since >= RECONNECT_STABLE_BYTES) {
                    attempt = 0;
                    awaiting_stable = false;
                }
            }

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
            /* 8.1: bounded, stop-aware backoff — a single interruptible
             * wait (radio_stop_sync() wakes it via task notify). */
            uint32_t delay = reconnect_delay_ms(attempt);
            attempt++;
            wait_or_stop(s, delay);
        }
    }

    /* 7.3: single exit label — signal exit and cleanup */
    xEventGroupSetBits(s->events, RADIO_EVT_STREAM_EXITED);
    s->stream_task = NULL;
    vTaskDelete(NULL);
}
