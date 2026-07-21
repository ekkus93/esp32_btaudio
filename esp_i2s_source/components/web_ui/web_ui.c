/*
 * web_ui device glue (WEB-1a): esp_http_server serving the embedded gzipped
 * SPA at GET / and an aggregated GET /api/status. See web_ui.h.
 *
 * The per-feature request handlers live in web_ui_{wifi,audio,radio,bt}.c and
 * are declared in web_ui_internal.h; this file owns GET /, GET /api/status, the
 * shared recv_body() helper, and web_ui_start() (the httpd config + handler
 * registration).
 */
#include "web_ui.h"
#include "web_ui_internal.h"
#include "wifi_mgr.h"
#include "bt_link.h"
#include "tone.h"
#include "radio.h"
#include "i2s_out.h"

#include <string.h>
#include <stdatomic.h>

#include "esp_http_server.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "web_ui";

static httpd_handle_t s_server;
static TaskHandle_t s_wroom_probe_task;

/* Embedded single-file SPA (gzip). Symbols from main's EMBED_FILES. */
extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[]   asm("_binary_index_html_gz_end");

/* WROOM32 identity cache (10.4 — don't block HTTP on UART).
 * Updated by a background probe task every 10 seconds. */
#include <stdatomic.h>

#define WROOM_PROBE_INTERVAL_MS 10000

typedef struct {
    char version[48];
    int64_t updated_us;
    _Atomic bool reachable;
    SemaphoreHandle_t mtx;
} wroom_status_cache_t;

static wroom_status_cache_t s_wroom_cache;

static void refresh_wroom(void)
{
    bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;
    char res[64] = {0};
    if (bt_link_send("VERSION", &st, res, sizeof(res), NULL, 0) == ESP_OK &&
        st == BT_LINK_CMD_DONE_OK) {
        strlcpy(s_wroom_cache.version, res, sizeof(s_wroom_cache.version));
        atomic_store(&s_wroom_cache.reachable, true);
    } else {
        atomic_store(&s_wroom_cache.reachable, false);
    }
    s_wroom_cache.updated_us = esp_timer_get_time();
}

/* Background probe task — refreshes the WROOM32 cache every 10s. */
static void wroom_probe_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(WROOM_PROBE_INTERVAL_MS));
        refresh_wroom();
    }
    vTaskDelete(NULL);
}

/* GET / — the gzipped SPA. */
static esp_err_t root_get(httpd_req_t *req)
{
    const size_t len = index_html_gz_end - index_html_gz_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)index_html_gz_start, len);
}

/* GET /api/status — aggregated S3 + last-known WROOM32 state. */
static esp_err_t status_get(httpd_req_t *req)
{
    wifi_mgr_info_t wi;
    wifi_mgr_get_info(&wi);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device", "esp-i2s-source");
    const esp_app_desc_t *app = esp_app_get_description();
    cJSON_AddStringToObject(root, "version", app ? app->version : "?");
    cJSON_AddNumberToObject(root, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(root, "heap_free", (double)esp_get_free_heap_size());

    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddStringToObject(wifi, "mode", wi.mode);
    if (wi.state[0]) cJSON_AddStringToObject(wifi, "state", wi.state);
    if (wi.ssid[0]) cJSON_AddStringToObject(wifi, "ssid", wi.ssid);
    cJSON_AddStringToObject(wifi, "ip", wi.ip);
    cJSON_AddNumberToObject(wifi, "rssi", wi.rssi);
    /* Concurrent SoftAP (control access point) info. */
    cJSON *ap = cJSON_AddObjectToObject(wifi, "ap");
    cJSON_AddBoolToObject(ap, "on", wi.ap_on);
    cJSON_AddBoolToObject(ap, "enabled", wifi_mgr_ap_enabled());
    cJSON_AddStringToObject(ap, "ssid", wi.ap_ssid);
    if (wi.ap_on) {
        cJSON_AddStringToObject(ap, "ip", wi.ap_ip);
        cJSON_AddNumberToObject(ap, "clients", wi.ap_clients);
    }

    cJSON *wroom = cJSON_AddObjectToObject(root, "wroom");
    cJSON_AddBoolToObject(wroom, "reachable", atomic_load(&s_wroom_cache.reachable));
    if (atomic_load(&s_wroom_cache.reachable)) {
        cJSON_AddStringToObject(wroom, "version", s_wroom_cache.version);
    }

    bool tone_on; int tone_hz;
    tone_get(&tone_on, &tone_hz);
    cJSON *tone = cJSON_AddObjectToObject(root, "tone");
    cJSON_AddBoolToObject(tone, "on", tone_on);
    cJSON_AddNumberToObject(tone, "hz", tone_hz);

    radio_status_t rs;
    radio_get_status(&rs);
    cJSON *radio = cJSON_AddObjectToObject(root, "radio");
    cJSON_AddBoolToObject(radio, "playing", rs.playing);
    cJSON_AddBoolToObject(radio, "buffering", rs.buffering);
    cJSON_AddStringToObject(radio, "codec", radio_codec_str(rs.codec));
    cJSON_AddStringToObject(radio, "station", rs.station);
    cJSON_AddStringToObject(radio, "title", rs.title);
    cJSON_AddStringToObject(radio, "url", rs.url);
    cJSON_AddNumberToObject(radio, "bitrate", rs.bitrate_kbps);
    cJSON_AddNumberToObject(radio, "bytes_in", (double)rs.bytes_in);
    cJSON_AddNumberToObject(radio, "ring_used", rs.ring_used);
    cJSON_AddNumberToObject(radio, "ring_cap", rs.ring_cap);
    cJSON_AddNumberToObject(radio, "reconnects", rs.reconnects);
    cJSON_AddNumberToObject(radio, "dec_rate", rs.dec_rate);
    cJSON_AddNumberToObject(radio, "dec_channels", rs.dec_channels);
    cJSON_AddNumberToObject(radio, "pcm_used", rs.pcm_used);
    cJSON_AddNumberToObject(radio, "pcm_cap", rs.pcm_cap);
    cJSON_AddNumberToObject(radio, "decode_errors", rs.decode_errors);
    cJSON_AddNumberToObject(radio, "prebuffer_ms", rs.prebuffer_ms);

    /* RADIO-2d: I2S output health — the on-source dropout signal. underrun_*
     * must stay flat and ring_peak below ring_cap for a clean endurance run. */
    i2s_out_stats_t is;
    i2s_out_get_stats(&is);
    cJSON *i2s = cJSON_AddObjectToObject(root, "i2s");
    cJSON_AddNumberToObject(i2s, "bytes_written", (double)is.bytes_written);
    cJSON_AddNumberToObject(i2s, "underrun_bytes", (double)is.underrun_bytes);
    cJSON_AddNumberToObject(i2s, "underrun_events", (double)is.underrun_events);
    cJSON_AddNumberToObject(i2s, "ring_peak", (double)is.ring_peak);
    cJSON_AddNumberToObject(i2s, "gain", i2s_out_get_gain());   /* pre-I2S volume % */

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return httpd_resp_send_500(req);

    httpd_resp_set_type(req, "application/json");
    esp_err_t e = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return e;
}

/* Read the full request body into buf (NUL-terminated). Shared by the feature
 * handlers via web_ui_internal.h. */
esp_err_t recv_body(httpd_req_t *req, char *buf, size_t buf_sz)
{
    int total = req->content_len;
    if (total <= 0 || (size_t)total >= buf_sz) return ESP_FAIL;
    int off = 0;
    while (off < total) {
        int r = httpd_req_recv(req, buf + off, total - off);
        if (r <= 0) return ESP_FAIL;
        off += r;
    }
    buf[off] = '\0';
    return ESP_OK;
}

esp_err_t web_ui_stop(void)
{
    /* Terminate the background probe task. */
    if (s_wroom_probe_task) {
        vTaskDelete(s_wroom_probe_task);
        s_wroom_probe_task = NULL;
    }
    /* Delete the cache mutex. */
    if (s_wroom_cache.mtx) {
        vSemaphoreDelete(s_wroom_cache.mtx);
        s_wroom_cache.mtx = NULL;
    }
    if (s_server) {
        esp_err_t err = httpd_stop(s_server);
        s_server = NULL;
        return err;
    }
    return ESP_OK;
}

/* Register a single URI handler, logging the method/URI on failure. */
static esp_err_t register_uri(httpd_handle_t server, const httpd_uri_t *uri)
{
    esp_err_t err = httpd_register_uri_handler(server, uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "route registration failed: %s %s: %s",
                 http_method_str(uri->method), uri->uri,
                 esp_err_to_name(err));
    }
    return err;
}

/* Register a URI handler, jumping to cleanup on failure. */
#define REGISTER_OR_FAIL(uri_ptr) do { \
    err = register_uri(s_server, (uri_ptr)); \
    if (err != ESP_OK) goto fail; \
} while(0)

/* Background WROOM32 probe task handle for cleanup. */

/* Centralized authorization dispatcher (FIX3 §5.4): every mutating route
 * registers with .handler = route_dispatch and .user_ctx pointing at a
 * static-lifetime web_route_ctx_t, so no feature handler can be reached
 * without going through this check first. */
esp_err_t route_dispatch(httpd_req_t *req)
{
    const web_route_ctx_t *ctx = (const web_route_ctx_t *)req->user_ctx;
    if (!ctx || !ctx->handler) {
        return ESP_FAIL;
    }
    if (ctx->auth_required && !web_ui_auth_check(req)) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
        return web_send_error(req, "401 Unauthorized", "AUTH_REQUIRED",
                              "A valid bearer token is required", false);
    }
    return ctx->handler(req);
}

static const web_route_ctx_t S_WIFI_POST       = { .handler = wifi_post,         .auth_required = true, .capability = "wifi" };
static const web_route_ctx_t S_APMODE_POST     = { .handler = apmode_post_h,     .auth_required = true, .capability = "wifi" };
static const web_route_ctx_t S_TONE_POST       = { .handler = tone_post,         .auth_required = true, .capability = "i2s" };
static const web_route_ctx_t S_TONE_DELETE     = { .handler = tone_delete,       .auth_required = true, .capability = "i2s" };
static const web_route_ctx_t S_RADIO_POST      = { .handler = radio_post,        .auth_required = true, .capability = "radio" };
static const web_route_ctx_t S_RADIO_DELETE    = { .handler = radio_delete,      .auth_required = true, .capability = "radio" };
static const web_route_ctx_t S_STATIONS_POST   = { .handler = stations_post_h,   .auth_required = true, .capability = "stations" };
static const web_route_ctx_t S_STATIONS_PUT    = { .handler = stations_put_h,    .auth_required = true, .capability = "stations" };
static const web_route_ctx_t S_STATIONS_DELETE = { .handler = stations_delete_h, .auth_required = true, .capability = "stations" };
static const web_route_ctx_t S_SCAN_POST       = { .handler = scan_post_h,       .auth_required = true, .capability = "ctrl" };
static const web_route_ctx_t S_VOLUME_POST     = { .handler = volume_post_h,     .auth_required = true, .capability = "i2s" };
static const web_route_ctx_t S_PREBUFFER_POST  = { .handler = prebuffer_post_h,  .auth_required = true, .capability = "radio" };
static const web_route_ctx_t S_BTVOLUME_POST   = { .handler = btvolume_post_h,   .auth_required = true, .capability = "bt_link" };
static const web_route_ctx_t S_CTRL_POST       = { .handler = ctrl_post_h,       .auth_required = true, .capability = "ctrl" };
static const web_route_ctx_t S_BT_POST         = { .handler = bt_post_h,         .auth_required = true, .capability = "bt_link" };
static const web_route_ctx_t S_CONSOLE_POST    = { .handler = console_post_h,    .auth_required = true, .capability = "bt_link" };

esp_err_t web_ui_start(void)
{
    /* Initialise authentication (FIX3 §5.2) — must succeed before the HTTP
     * server (or any other resource) starts. A failure here is never
     * silently downgraded to "server started, unauthenticated." */
    esp_err_t auth_err = web_ui_auth_init();
    if (auth_err != ESP_OK) {
        ESP_LOGE(TAG, "auth init failed: %s", esp_err_to_name(auth_err));
        printf("DIAG|AUTH|ERROR|stage=init,err=%s\n", esp_err_to_name(auth_err));
        fflush(stdout);
        return auth_err;
    }
    /* Initialise async operation queue (10.5). */
    web_ui_ops_init();

    /* Initialise the WROOM32 cache mutex. */
    s_wroom_cache.mtx = xSemaphoreCreateMutex();
    if (!s_wroom_cache.mtx) {
        ESP_LOGE(TAG, "failed to create wroom cache mutex");
        return ESP_ERR_NO_MEM;
    }
    /* Do the initial WROOM32 probe synchronously. */
    refresh_wroom();

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 29;   /* status/wifi/apmode/tone/radio/stations/volume/prebuffer/btvolume/ctrl/bt/console/root */

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t status_uri = {
        .uri = "/api/status", .method = HTTP_GET, .handler = status_get };
    const httpd_uri_t wifi_uri = {
        .uri = "/api/wifi", .method = HTTP_POST, .handler = route_dispatch,
        .user_ctx = (void *)&S_WIFI_POST };
    const httpd_uri_t tone_post_uri = {
        .uri = "/api/tone", .method = HTTP_POST, .handler = route_dispatch,
        .user_ctx = (void *)&S_TONE_POST };
    const httpd_uri_t tone_del_uri = {
        .uri = "/api/tone", .method = HTTP_DELETE, .handler = route_dispatch,
        .user_ctx = (void *)&S_TONE_DELETE };
    const httpd_uri_t radio_post_uri = {
        .uri = "/api/radio", .method = HTTP_POST, .handler = route_dispatch,
        .user_ctx = (void *)&S_RADIO_POST };
    const httpd_uri_t radio_del_uri = {
        .uri = "/api/radio", .method = HTTP_DELETE, .handler = route_dispatch,
        .user_ctx = (void *)&S_RADIO_DELETE };
    const httpd_uri_t st_get_uri = {
        .uri = "/api/stations", .method = HTTP_GET, .handler = stations_get_h };
    const httpd_uri_t st_post_uri = {
        .uri = "/api/stations", .method = HTTP_POST, .handler = route_dispatch,
        .user_ctx = (void *)&S_STATIONS_POST };
    const httpd_uri_t st_put_uri = {
        .uri = "/api/stations", .method = HTTP_PUT, .handler = route_dispatch,
        .user_ctx = (void *)&S_STATIONS_PUT };
    const httpd_uri_t st_del_uri = {
        .uri = "/api/stations", .method = HTTP_DELETE, .handler = route_dispatch,
        .user_ctx = (void *)&S_STATIONS_DELETE };
    const httpd_uri_t scan_post_uri = {
        .uri = "/api/scan", .method = HTTP_POST, .handler = route_dispatch,
        .user_ctx = (void *)&S_SCAN_POST };
    const httpd_uri_t apmode_post_uri = {
        .uri = "/api/apmode", .method = HTTP_POST, .handler = route_dispatch,
        .user_ctx = (void *)&S_APMODE_POST };
    const httpd_uri_t volume_post_uri = {
        .uri = "/api/volume", .method = HTTP_POST, .handler = route_dispatch,
        .user_ctx = (void *)&S_VOLUME_POST };
    const httpd_uri_t prebuffer_post_uri = {
        .uri = "/api/prebuffer", .method = HTTP_POST, .handler = route_dispatch,
        .user_ctx = (void *)&S_PREBUFFER_POST };
    const httpd_uri_t btvol_get_uri = {
        .uri = "/api/btvolume", .method = HTTP_GET, .handler = btvolume_get_h };
    const httpd_uri_t btvol_post_uri = {
        .uri = "/api/btvolume", .method = HTTP_POST, .handler = route_dispatch,
        .user_ctx = (void *)&S_BTVOLUME_POST };
    const httpd_uri_t ctrl_get_uri = {
        .uri = "/api/ctrl", .method = HTTP_GET, .handler = ctrl_get_h };
    const httpd_uri_t ctrl_post_uri = {
        .uri = "/api/ctrl", .method = HTTP_POST, .handler = route_dispatch,
        .user_ctx = (void *)&S_CTRL_POST };
    const httpd_uri_t bt_get_uri = {
        .uri = "/api/bt", .method = HTTP_GET, .handler = bt_get_h };
    const httpd_uri_t bt_post_uri = {
        .uri = "/api/bt", .method = HTTP_POST, .handler = route_dispatch,
        .user_ctx = (void *)&S_BT_POST };
    const httpd_uri_t console_post_uri = {
        .uri = "/api/console", .method = HTTP_POST, .handler = route_dispatch,
        .user_ctx = (void *)&S_CONSOLE_POST };
    const httpd_uri_t root_uri = {
        .uri = "/*", .method = HTTP_GET, .handler = root_get };

    REGISTER_OR_FAIL(&status_uri);
    REGISTER_OR_FAIL(&wifi_uri);
    REGISTER_OR_FAIL(&tone_post_uri);
    REGISTER_OR_FAIL(&tone_del_uri);
    REGISTER_OR_FAIL(&radio_post_uri);
    REGISTER_OR_FAIL(&radio_del_uri);
    REGISTER_OR_FAIL(&st_get_uri);
    REGISTER_OR_FAIL(&st_post_uri);
    REGISTER_OR_FAIL(&st_put_uri);
    REGISTER_OR_FAIL(&st_del_uri);
    REGISTER_OR_FAIL(&scan_post_uri);
    REGISTER_OR_FAIL(&apmode_post_uri);
    REGISTER_OR_FAIL(&volume_post_uri);
    REGISTER_OR_FAIL(&prebuffer_post_uri);
    REGISTER_OR_FAIL(&btvol_get_uri);
    REGISTER_OR_FAIL(&btvol_post_uri);
    REGISTER_OR_FAIL(&ctrl_get_uri);
    REGISTER_OR_FAIL(&ctrl_post_uri);
    REGISTER_OR_FAIL(&bt_get_uri);
    REGISTER_OR_FAIL(&bt_post_uri);
    REGISTER_OR_FAIL(&console_post_uri);
    REGISTER_OR_FAIL(&root_uri);  /* catch-all last */

    /* Create the background WROOM32 probe task (10.4 — don't block HTTP on UART). */
    if (xTaskCreate(wroom_probe_task, "wroom_probe", 4096, NULL,
                    tskIDLE_PRIORITY + 1, &s_wroom_probe_task) != pdPASS) {
        ESP_LOGW(TAG, "failed to create wroom probe task");
    }

    ESP_LOGI(TAG, "web UI up on port %d (%u B gzip SPA)", cfg.server_port,
             (unsigned)(index_html_gz_end - index_html_gz_start));
    printf("DIAG|WEB|READY|port=80\n");
    fflush(stdout);
    return ESP_OK;

fail:
    httpd_stop(s_server);
    s_server = NULL;
    return err;
}
