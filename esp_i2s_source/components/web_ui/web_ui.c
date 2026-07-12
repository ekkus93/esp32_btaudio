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

/* ---- Bluetooth state (REST, replaces the WebSocket) ------------------------
 * The WROOM32 pushes async lines over bt_link (scan results, paired-list items,
 * pairing prompts). We buffer them here (on_bt_event) and expose the state via
 * GET /api/bt, which the browser polls — no persistent WebSocket to exhaust the
 * lwIP socket pool. */
#define BT_MAX_DEV 20
typedef struct { char mac[20]; char name[36]; } bt_dev_t;
static bt_dev_t          s_bt_paired[BT_MAX_DEV];
static int               s_bt_paired_n;
static bt_dev_t          s_bt_disc[BT_MAX_DEV];
static int               s_bt_disc_n;
static char              s_bt_prompt[80];
static bool              s_bt_prompt_on;
static SemaphoreHandle_t s_bt_mtx;

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

/* ---- /api/scan: suspend A2DP, run a BT inquiry, restore (CTRL) ---- */

static esp_err_t scan_post_h(httpd_req_t *req)
{
    if (s_bt_mtx) {
        xSemaphoreTake(s_bt_mtx, portMAX_DELAY);
        s_bt_disc_n = 0;                 /* fresh discovery list */
        xSemaphoreGive(s_bt_mtx);
    }
    esp_err_t e = ctrl_scan();
    httpd_resp_set_type(req, "application/json");
    if (e == ESP_ERR_INVALID_STATE) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"scan already running\"}");
    } else if (e != ESP_OK) {
        httpd_resp_send_500(req);
    } else {
        /* Audio pauses ~20 s; discovered devices appear in GET /api/bt. */
        httpd_resp_sendstr(req, "{\"ok\":true}");
    }
    return ESP_OK;
}

/* ---- /api/apmode: control the concurrent AP — toggle (AP+STA vs STA-only)
 * and/or change the AP name/password. Accepts {enabled?} and/or {ssid, pass?}. */

static esp_err_t apmode_post_h(httpd_req_t *req)
{
    char body[160];
    if (recv_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_OK;
    }
    cJSON *j = cJSON_Parse(body);
    httpd_resp_set_type(req, "application/json");
    cJSON *en = j ? cJSON_GetObjectItem(j, "enabled") : NULL;
    const char *ssid = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "ssid")) : NULL;
    cJSON *passj = j ? cJSON_GetObjectItem(j, "pass") : NULL;
    const char *pass = cJSON_IsString(passj) ? passj->valuestring : NULL;

    if (ssid) {   /* rename / re-key the control AP */
        esp_err_t e = wifi_mgr_set_ap_config(ssid, pass ? pass : "");
        if (e != ESP_OK) {
            cJSON_Delete(j);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_sendstr(req,
                "{\"ok\":false,\"error\":\"SSID 1-32 chars; password blank or 8-64\"}");
            return ESP_OK;
        }
    }
    if (cJSON_IsBool(en)) {
        wifi_mgr_set_ap_enabled(cJSON_IsTrue(en));
    } else if (!ssid) {
        cJSON_Delete(j);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"missing enabled or ssid\"}");
        return ESP_OK;
    }
    cJSON_Delete(j);
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ---- /api/btvolume: WROOM32 post-mix VOLUME (0..100) over bt_link ---- */

/* Pull a standalone "KEY=" integer token out of a comma-separated WROOM32
 * STATUS data string (e.g. "...,RUN=1,VOL=12,..."). -1 if absent. */
static int parse_wroom_kv(const char *data, const char *key)
{
    size_t klen = strlen(key);
    const char *p = data;
    while ((p = strstr(p, key)) != NULL) {
        if (p == data || p[-1] == ',') return atoi(p + klen);
        p += klen;
    }
    return -1;
}

static int parse_wroom_vol(const char *data) { return parse_wroom_kv(data, "VOL="); }

static esp_err_t btvolume_get_h(httpd_req_t *req)
{
    bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;
    char data[BT_LINK_FIELD_MAX] = {0};
    bt_link_send("STATUS", &st, NULL, 0, data, sizeof(data));
    int vol = (st == BT_LINK_CMD_DONE_OK) ? parse_wroom_vol(data) : -1;
    char out[32];
    snprintf(out, sizeof(out), "{\"vol\":%d}", vol);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, out);
}

static esp_err_t btvolume_post_h(httpd_req_t *req)
{
    char body[64];
    if (recv_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_OK;
    }
    cJSON *j = cJSON_Parse(body);
    cJSON *v = j ? cJSON_GetObjectItem(j, "vol") : NULL;
    httpd_resp_set_type(req, "application/json");
    if (!cJSON_IsNumber(v)) {
        cJSON_Delete(j);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"missing vol\"}");
        return ESP_OK;
    }
    int vol = v->valueint;
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    cJSON_Delete(j);
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "VOLUME %d", vol);
    bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;
    bt_link_send(cmd, &st, NULL, 0, NULL, 0);
    /* Persist as the post-mix level so it survives reboot / A2DP reconnect —
     * the orchestrator re-asserts ctrl_cfg.volume on connect (the WROOM32
     * resets VOL to 40 on a fresh link). mac NULL => keep current sink. */
    if (st == BT_LINK_CMD_DONE_OK) {
        ctrl_cfg_t cur;
        ctrl_get_cfg(&cur);
        ctrl_set_sink(NULL, cur.autostart, vol);
    }
    char out[48];
    snprintf(out, sizeof(out), "{\"ok\":%s,\"vol\":%d}",
             st == BT_LINK_CMD_DONE_OK ? "true" : "false", vol);
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

/* ---- Bluetooth state buffering + REST (replaces the WebSocket) ---- */

static void bt_split(const char *data, char *mac, size_t macsz, char *name, size_t namesz)
{
    const char *comma = strchr(data, ',');
    if (comma) {
        size_t ml = (size_t)(comma - data);
        if (ml >= macsz) ml = macsz - 1;
        memcpy(mac, data, ml);
        mac[ml] = '\0';
        strlcpy(name, comma + 1, namesz);
    } else {
        strlcpy(mac, data, macsz);
        name[0] = '\0';
    }
}

/* Add (or upgrade the name of) a device in a list. Caller holds s_bt_mtx. */
static void bt_add(bt_dev_t *list, int *n, const char *mac, const char *name)
{
    for (int i = 0; i < *n; i++) {
        if (strcasecmp(list[i].mac, mac) == 0) {
            if (name[0] && !list[i].name[0]) strlcpy(list[i].name, name, sizeof(list[i].name));
            return;
        }
    }
    if (*n >= BT_MAX_DEV) return;
    strlcpy(list[*n].mac, mac, sizeof(list[*n].mac));
    strlcpy(list[*n].name, name, sizeof(list[*n].name));
    (*n)++;
}

static void bt_remove(bt_dev_t *list, int *n, const char *mac)
{
    for (int i = 0; i < *n; i++) {
        if (strcasecmp(list[i].mac, mac) == 0) {
            list[i] = list[--(*n)];
            return;
        }
    }
}

/* Standalone RUN=1 token from a WROOM32 STATUS data string (skips UNDERRUN*). */
static bool bt_status_running(const char *data)
{
    const char *p = data;
    while ((p = strstr(p, "RUN=")) != NULL) {
        if (p == data || p[-1] == ',') return p[4] == '1';
        p += 4;
    }
    return false;
}

/* bt_link async-line subscriber -> buffer into the BT state. Runs on the
 * bt_link task; keep it quick and non-blocking. */
static void on_bt_event(void *ctx, const bt_link_msg_t *m)
{
    (void)ctx;
    if (!s_bt_mtx) return;
    char mac[20], name[36];
    xSemaphoreTake(s_bt_mtx, portMAX_DELAY);
    if (m->status == BT_LINK_INFO && strcmp(m->command, "SCAN") == 0 &&
        strcmp(m->result, "RESULT") == 0) {
        bt_split(m->data, mac, sizeof(mac), name, sizeof(name));
        bt_add(s_bt_disc, &s_bt_disc_n, mac, name);
    } else if (m->status == BT_LINK_INFO && strcmp(m->command, "PAIRED") == 0 &&
               strcmp(m->result, "ITEM") == 0) {
        bt_split(m->data, mac, sizeof(mac), name, sizeof(name));
        bt_add(s_bt_paired, &s_bt_paired_n, mac, name);
    } else if (m->status == BT_LINK_EVENT && strcmp(m->command, "PAIR") == 0) {
        if (strcmp(m->result, "CONFIRM") == 0) {
            strlcpy(s_bt_prompt, m->data, sizeof(s_bt_prompt));
            s_bt_prompt_on = true;
        } else if (strcmp(m->result, "SUCCESS") == 0 || strcmp(m->result, "FAILED") == 0) {
            s_bt_prompt_on = false;
        }
    }
    xSemaphoreGive(s_bt_mtx);
}

static void bt_dev_array(cJSON *arr, const bt_dev_t *list, int n)
{
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "mac", list[i].mac);
        cJSON_AddStringToObject(o, "name", list[i].name);
        cJSON_AddItemToArray(arr, o);
    }
}

/* GET /api/bt — polled BT snapshot: connection, scan-in-progress, pairing
 * prompt, paired + discovered device lists. */
static esp_err_t bt_get_h(httpd_req_t *req)
{
    bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;
    char sdata[BT_LINK_FIELD_MAX] = {0};
    bt_link_send("STATUS", &st, NULL, 0, sdata, sizeof(sdata));
    bool connected = (st == BT_LINK_CMD_DONE_OK) && bt_status_running(sdata);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "connected", connected);
    cJSON_AddBoolToObject(root, "scanning", ctrl_scan_active());
    xSemaphoreTake(s_bt_mtx, portMAX_DELAY);
    if (s_bt_prompt_on) cJSON_AddStringToObject(root, "prompt", s_bt_prompt);
    bt_dev_array(cJSON_AddArrayToObject(root, "paired"), s_bt_paired, s_bt_paired_n);
    bt_dev_array(cJSON_AddArrayToObject(root, "discovered"), s_bt_disc, s_bt_disc_n);
    xSemaphoreGive(s_bt_mtx);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    esp_err_t e = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return e;
}

/* POST /api/bt {action, mac?} — connect/disconnect/pair/unpair/pin/refresh. */
/* A manual "connect" is async on the WROOM32: CONNECT returns INITIATED, the
 * A2DP link comes up seconds later, and the WROOM32 resets its volume to 40 on
 * that fresh link. Poll STATUS until RUN=1, then re-assert the persisted
 * post-mix volume so a hand-initiated connect lands at the user's saved level
 * (mirrors the orchestrator's autostart path). One at a time. */
static TaskHandle_t s_conn_vol_task;

static void connect_volume_task(void *arg)
{
    (void)arg;
    ctrl_cfg_t c;
    ctrl_get_cfg(&c);
    for (int i = 0; i < 30; i++) {          /* ~15 s at 500 ms */
        vTaskDelay(pdMS_TO_TICKS(500));
        bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;
        char data[BT_LINK_FIELD_MAX] = {0};
        bt_link_send("STATUS", &st, NULL, 0, data, sizeof(data));
        if (st == BT_LINK_CMD_DONE_OK && parse_wroom_kv(data, "RUN=") == 1) {
            char cmd[16];
            snprintf(cmd, sizeof(cmd), "VOLUME %u", c.volume);
            bt_link_send(cmd, &st, NULL, 0, NULL, 0);
            break;
        }
    }
    s_conn_vol_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t bt_post_h(httpd_req_t *req)
{
    char body[128];
    if (recv_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_OK;
    }
    cJSON *j = cJSON_Parse(body);
    const char *action = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "action")) : NULL;
    const char *mac = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "mac")) : NULL;
    char cmd[64] = {0};

    bool is_connect = action && !strcmp(action, "connect") && mac;
    if (is_connect) snprintf(cmd, sizeof(cmd), "CONNECT %s", mac);
    else if (action && !strcmp(action, "disconnect")) strlcpy(cmd, "DISCONNECT", sizeof(cmd));
    else if (action && !strcmp(action, "pair") && mac) snprintf(cmd, sizeof(cmd), "PAIR %s", mac);
    else if (action && !strcmp(action, "unpair") && mac) snprintf(cmd, sizeof(cmd), "UNPAIR %s", mac);
    else if (action && !strcmp(action, "pin_accept")) strlcpy(cmd, "CONFIRM_PIN ACCEPT", sizeof(cmd));
    else if (action && !strcmp(action, "pin_reject")) strlcpy(cmd, "CONFIRM_PIN REJECT", sizeof(cmd));
    else if (action && !strcmp(action, "refresh")) strlcpy(cmd, "PAIRED", sizeof(cmd));

    /* Local state effects. */
    if (cmd[0] && s_bt_mtx) {
        xSemaphoreTake(s_bt_mtx, portMAX_DELAY);
        if (!strcmp(action, "refresh")) s_bt_paired_n = 0;
        else if (!strcmp(action, "unpair") && mac) bt_remove(s_bt_paired, &s_bt_paired_n, mac);
        else if (!strcmp(action, "pin_accept") || !strcmp(action, "pin_reject")) s_bt_prompt_on = false;
        xSemaphoreGive(s_bt_mtx);
    }
    cJSON_Delete(j);

    httpd_resp_set_type(req, "application/json");
    if (!cmd[0]) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"bad action\"}");
        return ESP_OK;
    }
    bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;
    char result[BT_LINK_FIELD_MAX] = {0};
    bt_link_send(cmd, &st, result, sizeof(result), NULL, 0);
    /* Connect accepted (INITIATED): re-assert the saved post-mix volume once the
     * A2DP link is actually up, so it isn't left at the WROOM32's fresh-link 40. */
    if (is_connect && st == BT_LINK_CMD_DONE_OK && s_conn_vol_task == NULL) {
        xTaskCreate(connect_volume_task, "connvol", 4096, NULL,
                    tskIDLE_PRIORITY + 3, &s_conn_vol_task);
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", st == BT_LINK_CMD_DONE_OK);
    cJSON_AddStringToObject(r, "result", result);
    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    httpd_resp_sendstr(req, s ? s : "{\"ok\":false}");
    if (s) cJSON_free(s);
    return ESP_OK;
}

/* POST /api/console {cmd} — run a raw WROOM32 command, return its response. */
static esp_err_t console_post_h(httpd_req_t *req)
{
    char body[BT_LINK_FIELD_MAX + 32];
    if (recv_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_OK;
    }
    cJSON *j = cJSON_Parse(body);
    const char *incmd = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "cmd")) : NULL;
    httpd_resp_set_type(req, "application/json");
    if (!incmd || !incmd[0]) {
        cJSON_Delete(j);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"missing cmd\"}");
        return ESP_OK;
    }
    char cmd[BT_LINK_FIELD_MAX];
    strlcpy(cmd, incmd, sizeof(cmd));
    cJSON_Delete(j);

    bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;
    char result[BT_LINK_FIELD_MAX] = {0}, data[BT_LINK_FIELD_MAX] = {0};
    bt_link_send(cmd, &st, result, sizeof(result), data, sizeof(data));
    const char *status = (st == BT_LINK_CMD_DONE_OK) ? "OK"
                       : (st == BT_LINK_CMD_DONE_ERR) ? "ERR" : "TIMEOUT";
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "status", status);
    cJSON_AddStringToObject(o, "result", result);
    cJSON_AddStringToObject(o, "data", data);
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    httpd_resp_sendstr(req, s ? s : "{\"status\":\"TIMEOUT\"}");
    if (s) cJSON_free(s);
    return ESP_OK;
}

esp_err_t web_ui_start(void)
{
    refresh_wroom();

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 28;   /* status/wifi/apmode/tone/radio/stations/volume/btvolume/ctrl/bt/console/root */

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
    s_bt_mtx = xSemaphoreCreateMutex();
    bt_link_subscribe(on_bt_event, NULL);
    {
        bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;
        bt_link_send("PAIRED", &st, NULL, 0, NULL, 0);   /* items arrive via on_bt_event */
    }

    ESP_LOGI(TAG, "web UI up on port %d (%u B gzip SPA)", cfg.server_port,
             (unsigned)(index_html_gz_end - index_html_gz_start));
    printf("DIAG|WEB|READY|port=80\n");
    fflush(stdout);
    return ESP_OK;
}
