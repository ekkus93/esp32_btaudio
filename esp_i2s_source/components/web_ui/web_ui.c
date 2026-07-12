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

#include "esp_http_server.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"
#include "cJSON.h"
#include <stdio.h>

static const char *TAG = "web_ui";

static httpd_handle_t s_server;

/* Embedded single-file SPA (gzip). Symbols from main's EMBED_FILES. */
extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[]   asm("_binary_index_html_gz_end");

/* WROOM32 identity, cached once at startup (a live bt_link query per status
 * poll would block the httpd handler on UART). Refreshed lazily on demand. */
static char s_wroom_version[48];
static bool s_wroom_reachable;

static void refresh_wroom(void)
{
    bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;
    char res[64] = {0};
    if (bt_link_send("VERSION", &st, res, sizeof(res), NULL, 0) == ESP_OK &&
        st == BT_LINK_CMD_DONE_OK) {
        strlcpy(s_wroom_version, res, sizeof(s_wroom_version));
        s_wroom_reachable = true;
    } else {
        s_wroom_reachable = false;
    }
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
    cJSON_AddStringToObject(ap, "pass", wi.ap_pass);
    if (wi.ap_on) {
        cJSON_AddStringToObject(ap, "ip", wi.ap_ip);
        cJSON_AddNumberToObject(ap, "clients", wi.ap_clients);
    }

    cJSON *wroom = cJSON_AddObjectToObject(root, "wroom");
    cJSON_AddBoolToObject(wroom, "reachable", s_wroom_reachable);
    if (s_wroom_reachable) cJSON_AddStringToObject(wroom, "version", s_wroom_version);

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

esp_err_t web_ui_start(void)
{
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
        .uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_post };
    const httpd_uri_t tone_post_uri = {
        .uri = "/api/tone", .method = HTTP_POST, .handler = tone_post };
    const httpd_uri_t tone_del_uri = {
        .uri = "/api/tone", .method = HTTP_DELETE, .handler = tone_delete };
    const httpd_uri_t radio_post_uri = {
        .uri = "/api/radio", .method = HTTP_POST, .handler = radio_post };
    const httpd_uri_t radio_del_uri = {
        .uri = "/api/radio", .method = HTTP_DELETE, .handler = radio_delete };
    const httpd_uri_t st_get_uri = {
        .uri = "/api/stations", .method = HTTP_GET, .handler = stations_get_h };
    const httpd_uri_t st_post_uri = {
        .uri = "/api/stations", .method = HTTP_POST, .handler = stations_post_h };
    const httpd_uri_t st_put_uri = {
        .uri = "/api/stations", .method = HTTP_PUT, .handler = stations_put_h };
    const httpd_uri_t st_del_uri = {
        .uri = "/api/stations", .method = HTTP_DELETE, .handler = stations_delete_h };
    const httpd_uri_t scan_post_uri = {
        .uri = "/api/scan", .method = HTTP_POST, .handler = scan_post_h };
    const httpd_uri_t apmode_post_uri = {
        .uri = "/api/apmode", .method = HTTP_POST, .handler = apmode_post_h };
    const httpd_uri_t volume_post_uri = {
        .uri = "/api/volume", .method = HTTP_POST, .handler = volume_post_h };
    const httpd_uri_t prebuffer_post_uri = {
        .uri = "/api/prebuffer", .method = HTTP_POST, .handler = prebuffer_post_h };
    const httpd_uri_t btvol_get_uri = {
        .uri = "/api/btvolume", .method = HTTP_GET, .handler = btvolume_get_h };
    const httpd_uri_t btvol_post_uri = {
        .uri = "/api/btvolume", .method = HTTP_POST, .handler = btvolume_post_h };
    const httpd_uri_t ctrl_get_uri = {
        .uri = "/api/ctrl", .method = HTTP_GET, .handler = ctrl_get_h };
    const httpd_uri_t ctrl_post_uri = {
        .uri = "/api/ctrl", .method = HTTP_POST, .handler = ctrl_post_h };
    const httpd_uri_t bt_get_uri = {
        .uri = "/api/bt", .method = HTTP_GET, .handler = bt_get_h };
    const httpd_uri_t bt_post_uri = {
        .uri = "/api/bt", .method = HTTP_POST, .handler = bt_post_h };
    const httpd_uri_t console_post_uri = {
        .uri = "/api/console", .method = HTTP_POST, .handler = console_post_h };
    const httpd_uri_t root_uri = {
        .uri = "/*", .method = HTTP_GET, .handler = root_get };
    httpd_register_uri_handler(s_server, &status_uri);
    httpd_register_uri_handler(s_server, &wifi_uri);
    httpd_register_uri_handler(s_server, &tone_post_uri);
    httpd_register_uri_handler(s_server, &tone_del_uri);
    httpd_register_uri_handler(s_server, &radio_post_uri);
    httpd_register_uri_handler(s_server, &radio_del_uri);
    httpd_register_uri_handler(s_server, &st_get_uri);
    httpd_register_uri_handler(s_server, &st_post_uri);
    httpd_register_uri_handler(s_server, &st_put_uri);
    httpd_register_uri_handler(s_server, &st_del_uri);
    httpd_register_uri_handler(s_server, &scan_post_uri);
    httpd_register_uri_handler(s_server, &apmode_post_uri);
    httpd_register_uri_handler(s_server, &volume_post_uri);
    httpd_register_uri_handler(s_server, &prebuffer_post_uri);
    httpd_register_uri_handler(s_server, &btvol_get_uri);
    httpd_register_uri_handler(s_server, &btvol_post_uri);
    httpd_register_uri_handler(s_server, &ctrl_get_uri);
    httpd_register_uri_handler(s_server, &ctrl_post_uri);
    httpd_register_uri_handler(s_server, &bt_get_uri);
    httpd_register_uri_handler(s_server, &bt_post_uri);
    httpd_register_uri_handler(s_server, &console_post_uri);
    httpd_register_uri_handler(s_server, &root_uri);  /* catch-all last */

    /* Buffer WROOM32 async lines (scan/paired/prompt) into the BT state for
     * GET /api/bt, then prime the paired list. */
    web_ui_bt_init();

    ESP_LOGI(TAG, "web UI up on port %d (%u B gzip SPA)", cfg.server_port,
             (unsigned)(index_html_gz_end - index_html_gz_start));
    printf("DIAG|WEB|READY|port=80\n");
    fflush(stdout);
    return ESP_OK;
}
