/*
 * web_ui device glue (WEB-1a): esp_http_server serving the embedded gzipped
 * SPA at GET / and an aggregated GET /api/status. See web_ui.h.
 */
#include "web_ui.h"
#include "wifi_mgr.h"
#include "bt_link.h"
#include "tone.h"
#include "radio.h"
#include "stations.h"
#include "station_store.h"
#include "i2s_out.h"
#include "ctrl.h"

#include <string.h>

#include "esp_http_server.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

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

/* Read the full request body into buf (NUL-terminated). */
static esp_err_t recv_body(httpd_req_t *req, char *buf, size_t buf_sz)
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

/* Deferred provisioning: apply the creds ~400 ms after the HTTP response, so
 * the reply flushes over the (soon-to-drop) SoftAP link before AP->STA. */
static char s_prov_ssid[WIFI_MGR_SSID_MAX + 1];
static char s_prov_pass[WIFI_MGR_PASS_MAX + 1];

static void provision_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(400));
    esp_err_t e = wifi_mgr_set_creds(s_prov_ssid, s_prov_pass);
    ESP_LOGI(TAG, "deferred provision \"%s\": %s", s_prov_ssid, esp_err_to_name(e));
    vTaskDelete(NULL);
}

/* POST /api/wifi {ssid, pass} — validate, reply, then switch AP->STA. */
static esp_err_t wifi_post(httpd_req_t *req)
{
    char body[256];
    if (recv_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_OK;
    }
    cJSON *j = cJSON_Parse(body);
    const char *ssid = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "ssid")) : NULL;
    const char *pass = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "pass")) : NULL;

    size_t sl = ssid ? strlen(ssid) : 0;
    size_t pl = pass ? strlen(pass) : 0;
    if (sl == 0 || sl > WIFI_MGR_SSID_MAX || (pl != 0 && (pl < 8 || pl > WIFI_MGR_PASS_MAX))) {
        cJSON_Delete(j);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"ssid 1..32, pass \\\"\\\" or 8..63\"}");
        return ESP_OK;
    }

    strlcpy(s_prov_ssid, ssid, sizeof(s_prov_ssid));
    strlcpy(s_prov_pass, pass ? pass : "", sizeof(s_prov_pass));
    cJSON_Delete(j);

    /* Reply BEFORE switching so the client sees success over the AP. */
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"host\":\"esp-i2s-source.local\"}");

    /* Apply asynchronously; the AP tears down as STA comes up. */
    xTaskCreate(provision_task, "provision", 4096, NULL, tskIDLE_PRIORITY + 3, NULL);
    return ESP_OK;
}

/* POST /api/tone {hz} — enable the test tone; DELETE /api/tone — silence it. */
static esp_err_t tone_post(httpd_req_t *req)
{
    char body[128];
    int hz = TONE_HZ_DEFAULT;
    if (recv_body(req, body, sizeof(body)) == ESP_OK) {
        cJSON *j = cJSON_Parse(body);
        cJSON *h = j ? cJSON_GetObjectItem(j, "hz") : NULL;
        if (cJSON_IsNumber(h)) hz = h->valueint;
        cJSON_Delete(j);
    }
    tone_set(hz);
    bool on; int cur;
    tone_get(&on, &cur);
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"on\":true,\"hz\":%d}", cur);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, resp);
}

static esp_err_t tone_delete(httpd_req_t *req)
{
    tone_off();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"on\":false}");
}

/* POST /api/radio {url} — resolve + play; DELETE /api/radio — stop. The play/
 * stop paths can fetch a playlist or wait on a reconnect, so run them off the
 * httpd worker. */
static char s_radio_url[RADIO_URL_MAX];

static void radio_play_task(void *arg)
{
    (void)arg;
    radio_play(s_radio_url);
    vTaskDelete(NULL);
}
static void radio_stop_task(void *arg)
{
    (void)arg;
    radio_stop();
    vTaskDelete(NULL);
}

static esp_err_t radio_post(httpd_req_t *req)
{
    char body[RADIO_URL_MAX + 64];
    if (recv_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_OK;
    }
    cJSON *j = cJSON_Parse(body);
    const char *url = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "url")) : NULL;
    if (!url || !url[0] || strlen(url) >= RADIO_URL_MAX) {
        cJSON_Delete(j);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"missing/oversized url\"}");
        return ESP_OK;
    }
    strlcpy(s_radio_url, url, sizeof(s_radio_url));
    /* Optional station index: record it so CTRL-1 autostart can resume it. */
    cJSON *id = cJSON_GetObjectItem(j, "id");
    if (cJSON_IsNumber(id)) ctrl_note_station(id->valueint);
    cJSON_Delete(j);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    xTaskCreate(radio_play_task, "radioplay", 4096, NULL, tskIDLE_PRIORITY + 3, NULL);
    return ESP_OK;
}

static esp_err_t radio_delete(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    xTaskCreate(radio_stop_task, "radiostop", 4096, NULL, tskIDLE_PRIORITY + 3, NULL);
    return ESP_OK;
}

/* ---- /api/volume: S3 pre-I2S software gain (0..100) ---- */

static esp_err_t volume_post_h(httpd_req_t *req)
{
    char body[64];
    if (recv_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_OK;
    }
    cJSON *j = cJSON_Parse(body);
    cJSON *pct = j ? cJSON_GetObjectItem(j, "pct") : NULL;
    httpd_resp_set_type(req, "application/json");
    if (!cJSON_IsNumber(pct)) {
        cJSON_Delete(j);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"missing pct\"}");
        return ESP_OK;
    }
    i2s_out_set_gain(pct->valueint);       /* clamps to [0,100] */
    cJSON_Delete(j);
    char out[48];
    snprintf(out, sizeof(out), "{\"ok\":true,\"pct\":%d}", i2s_out_get_gain());
    httpd_resp_sendstr(req, out);
    return ESP_OK;
}

/* ---- /api/ctrl: orchestration config (CTRL-1b) ---- */

static esp_err_t ctrl_get_h(httpd_req_t *req)
{
    ctrl_cfg_t c;
    ctrl_get_cfg(&c);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "sink_mac", c.sink_mac);
    cJSON_AddBoolToObject(root, "autostart", c.autostart);
    cJSON_AddNumberToObject(root, "last_station", c.last_station);
    cJSON_AddNumberToObject(root, "volume", c.volume);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    esp_err_t e = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return e;
}

static esp_err_t ctrl_post_h(httpd_req_t *req)
{
    char body[256];
    if (recv_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_OK;
    }
    cJSON *j = cJSON_Parse(body);
    /* Carry current values for any field the caller omits. */
    ctrl_cfg_t cur;
    ctrl_get_cfg(&cur);
    const char *mac = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "sink_mac")) : NULL;
    cJSON *as = j ? cJSON_GetObjectItem(j, "autostart") : NULL;
    bool autostart = cJSON_IsBool(as) ? cJSON_IsTrue(as) : (bool)cur.autostart;
    cJSON *vol = j ? cJSON_GetObjectItem(j, "volume") : NULL;
    int volume = cJSON_IsNumber(vol) ? vol->valueint : -1;   /* -1 = keep current */
    esp_err_t e = ctrl_set_sink(mac, autostart, volume);
    cJSON_Delete(j);
    httpd_resp_set_type(req, "application/json");
    if (e != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"invalid mac\"}");
    } else {
        httpd_resp_sendstr(req, "{\"ok\":true}");
    }
    return ESP_OK;
}

/* ---- /api/stations CRUD (RADIO-1c) ---- */

static int station_id_param(httpd_req_t *req)
{
    char q[48], v[12];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "id", v, sizeof(v)) == ESP_OK) {
        return atoi(v);
    }
    return -1;
}

static esp_err_t stations_get_h(httpd_req_t *req)
{
    cJSON *arr = cJSON_CreateArray();
    int n = stations_count();
    for (int i = 0; i < n; i++) {
        char name[STATION_NAME_MAX], url[STATION_URL_MAX];
        if (!stations_get(i, name, sizeof(name), url, sizeof(url))) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "id", i);
        cJSON_AddStringToObject(o, "name", name);
        cJSON_AddStringToObject(o, "url", url);
        cJSON_AddItemToArray(arr, o);
    }
    char *body = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!body) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    esp_err_t e = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return e;
}

/* Parse {name,url} from the body into caller buffers; returns url ptr or NULL. */
static bool station_body(httpd_req_t *req, char *name, size_t nsz, char *url, size_t usz)
{
    char b[STATION_URL_MAX + STATION_NAME_MAX + 64];
    if (recv_body(req, b, sizeof(b)) != ESP_OK) return false;
    cJSON *j = cJSON_Parse(b);
    const char *jn = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "name")) : NULL;
    const char *ju = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "url")) : NULL;
    name[0] = url[0] = '\0';
    if (jn) strlcpy(name, jn, nsz);
    if (ju) strlcpy(url, ju, usz);
    bool ok = ju && ju[0];
    cJSON_Delete(j);
    return ok;
}

static void station_reply(httpd_req_t *req, bool ok, int id)
{
    char r[48];
    httpd_resp_set_type(req, "application/json");
    if (ok) {
        snprintf(r, sizeof(r), "{\"ok\":true,\"id\":%d}", id);
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        strlcpy(r, "{\"ok\":false,\"error\":\"invalid/duplicate/full\"}", sizeof(r));
    }
    httpd_resp_sendstr(req, r);
}

static esp_err_t stations_post_h(httpd_req_t *req)
{
    char name[STATION_NAME_MAX], url[STATION_URL_MAX];
    if (!station_body(req, name, sizeof(name), url, sizeof(url))) {
        station_reply(req, false, -1);
        return ESP_OK;
    }
    int id = stations_add(name, url);
    station_reply(req, id >= 0, id);
    return ESP_OK;
}

static esp_err_t stations_put_h(httpd_req_t *req)
{
    int id = station_id_param(req);
    char name[STATION_NAME_MAX], url[STATION_URL_MAX];
    if (id < 0 || !station_body(req, name, sizeof(name), url, sizeof(url))) {
        station_reply(req, false, id);
        return ESP_OK;
    }
    station_reply(req, stations_update(id, name, url), id);
    return ESP_OK;
}

static esp_err_t stations_delete_h(httpd_req_t *req)
{
    int id = station_id_param(req);
    station_reply(req, id >= 0 && stations_remove(id), id);
    return ESP_OK;
}

/* ---- WebSocket /ws: terminal I/O (term_in/term_out) + EVENT feed ---- */

#define WS_MAX_CLIENTS 4
static int s_ws_fds[WS_MAX_CLIENTS] = { -1, -1, -1, -1 };

static void ws_track(int fd)
{
    for (int i = 0; i < WS_MAX_CLIENTS; i++) if (s_ws_fds[i] == fd) return;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) if (s_ws_fds[i] < 0) { s_ws_fds[i] = fd; return; }
}
static void ws_untrack(int fd)
{
    for (int i = 0; i < WS_MAX_CLIENTS; i++) if (s_ws_fds[i] == fd) s_ws_fds[i] = -1;
}

static esp_err_t ws_send_text(int fd, const char *s)
{
    httpd_ws_frame_t f = {
        .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)s, .len = strlen(s) };
    return httpd_ws_send_frame_async(s_server, fd, &f);
}

/* Async broadcast: httpd_ws_send_frame_async must run on the httpd task, so a
 * bt_link event (bt_link task context) queues work rather than sending inline. */
typedef struct { int fd; char *json; } ws_push_t;

static void ws_push_work(void *arg)
{
    ws_push_t *p = arg;
    if (ws_send_text(p->fd, p->json) != ESP_OK) ws_untrack(p->fd);
    free(p->json);
    free(p);
}

static void ws_broadcast(const char *json)
{
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        int fd = s_ws_fds[i];
        if (fd < 0) continue;
        ws_push_t *p = malloc(sizeof(ws_push_t));
        if (!p) continue;
        p->fd = fd;
        p->json = strdup(json);
        if (!p->json || httpd_queue_work(s_server, ws_push_work, p) != ESP_OK) {
            free(p->json);
            free(p);
        }
    }
}

/* bt_link async-line subscriber -> {type:"event"|"info",...} to all WS clients.
 * EVENT = pairing prompts / state; INFO = scan results, paired-list items. */
static void on_bt_event(void *ctx, const bt_link_msg_t *m)
{
    (void)ctx;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "type", (m->status == BT_LINK_INFO) ? "info" : "event");
    cJSON_AddStringToObject(j, "command", m->command);
    cJSON_AddStringToObject(j, "result", m->result);
    cJSON_AddStringToObject(j, "data", m->data);
    char *s = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (s) { ws_broadcast(s); cJSON_free(s); }
}

static void send_term_out(httpd_req_t *req, const char *cmd,
                          bt_link_cmd_state_t st, const char *result, const char *data)
{
    const char *status = (st == BT_LINK_CMD_DONE_OK) ? "OK"
                       : (st == BT_LINK_CMD_DONE_ERR) ? "ERR" : "TIMEOUT";
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "term_out");
    cJSON_AddStringToObject(o, "cmd", cmd);
    cJSON_AddStringToObject(o, "status", status);
    cJSON_AddStringToObject(o, "result", result);
    cJSON_AddStringToObject(o, "data", data ? data : "");
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!s) return;
    httpd_ws_frame_t out = {
        .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)s, .len = strlen(s) };
    httpd_ws_send_frame(req, &out);
    cJSON_free(s);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {          /* handshake */
        ws_track(httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    httpd_ws_frame_t frame = { .type = HTTPD_WS_TYPE_TEXT };
    if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK) return ESP_FAIL;
    if (frame.len == 0 || frame.len > 480) return ESP_OK;

    uint8_t buf[512];
    frame.payload = buf;
    if (httpd_ws_recv_frame(req, &frame, sizeof(buf) - 1) != ESP_OK) return ESP_FAIL;
    buf[frame.len] = '\0';

    cJSON *j = cJSON_Parse((char *)buf);
    const char *type = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "type")) : NULL;
    const char *data = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "data")) : NULL;
    if (type && strcmp(type, "term_in") == 0 && data && data[0]) {
        char cmd[BT_LINK_FIELD_MAX];
        strlcpy(cmd, data, sizeof(cmd));
        cJSON_Delete(j);
        ws_track(httpd_req_to_sockfd(req));
        bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;
        char result[BT_LINK_FIELD_MAX] = {0};
        char rdata[BT_LINK_FIELD_MAX] = {0};
        bt_link_send(cmd, &st, result, sizeof(result), rdata, sizeof(rdata));
        send_term_out(req, cmd, st, result, rdata);
        return ESP_OK;
    }
    cJSON_Delete(j);
    return ESP_OK;
}

esp_err_t web_ui_start(void)
{
    refresh_wroom();

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 20;   /* status/wifi/tone/radio/stations(x4)/ws/root */

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
    const httpd_uri_t volume_post_uri = {
        .uri = "/api/volume", .method = HTTP_POST, .handler = volume_post_h };
    const httpd_uri_t ctrl_get_uri = {
        .uri = "/api/ctrl", .method = HTTP_GET, .handler = ctrl_get_h };
    const httpd_uri_t ctrl_post_uri = {
        .uri = "/api/ctrl", .method = HTTP_POST, .handler = ctrl_post_h };
    const httpd_uri_t ws_uri = {
        .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true };
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
    httpd_register_uri_handler(s_server, &volume_post_uri);
    httpd_register_uri_handler(s_server, &ctrl_get_uri);
    httpd_register_uri_handler(s_server, &ctrl_post_uri);
    httpd_register_uri_handler(s_server, &ws_uri);
    httpd_register_uri_handler(s_server, &root_uri);  /* catch-all last */

    /* Fan WROOM32 EVENT lines out to WS clients (WEB-1c live feed). */
    bt_link_subscribe(on_bt_event, NULL);

    ESP_LOGI(TAG, "web UI up on port %d (%u B gzip SPA)", cfg.server_port,
             (unsigned)(index_html_gz_end - index_html_gz_start));
    printf("DIAG|WEB|READY|port=80\n");
    fflush(stdout);
    return ESP_OK;
}
