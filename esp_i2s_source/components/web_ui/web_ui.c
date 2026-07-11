/*
 * web_ui device glue (WEB-1a): esp_http_server serving the embedded gzipped
 * SPA at GET / and an aggregated GET /api/status. See web_ui.h.
 */
#include "web_ui.h"
#include "wifi_mgr.h"
#include "bt_link.h"
#include "tone.h"

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
    if (bt_link_send("VERSION", &st, res, sizeof(res)) == ESP_OK &&
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

/* bt_link EVENT subscriber -> {type:"event",...} to all WS clients. */
static void on_bt_event(void *ctx, const bt_link_msg_t *m)
{
    (void)ctx;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "type", "event");
    cJSON_AddStringToObject(j, "command", m->command);
    cJSON_AddStringToObject(j, "result", m->result);
    cJSON_AddStringToObject(j, "data", m->data);
    char *s = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (s) { ws_broadcast(s); cJSON_free(s); }
}

static void send_term_out(httpd_req_t *req, const char *cmd,
                          bt_link_cmd_state_t st, const char *result)
{
    const char *status = (st == BT_LINK_CMD_DONE_OK) ? "OK"
                       : (st == BT_LINK_CMD_DONE_ERR) ? "ERR" : "TIMEOUT";
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "term_out");
    cJSON_AddStringToObject(o, "cmd", cmd);
    cJSON_AddStringToObject(o, "status", status);
    cJSON_AddStringToObject(o, "result", result);
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
        bt_link_send(cmd, &st, result, sizeof(result));  /* events still flow */
        send_term_out(req, cmd, st, result);
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
    const httpd_uri_t ws_uri = {
        .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true };
    const httpd_uri_t root_uri = {
        .uri = "/*", .method = HTTP_GET, .handler = root_get };
    httpd_register_uri_handler(s_server, &status_uri);
    httpd_register_uri_handler(s_server, &wifi_uri);
    httpd_register_uri_handler(s_server, &tone_post_uri);
    httpd_register_uri_handler(s_server, &tone_del_uri);
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
