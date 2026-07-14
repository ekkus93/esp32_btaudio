/*
 * web_ui_bt — Bluetooth / WROOM32 endpoints, split out of web_ui.c. Owns the
 * BT state buffered from bt_link async lines (scan/paired/pairing-prompt) and
 * the console INFO capture, and serves everything that talks to the WROOM32:
 *   /api/bt (GET/POST), /api/console, /api/scan, /api/btvolume, /api/ctrl.
 *
 * All state that these handlers share (s_bt_*, s_console_*, s_bt_mtx) lives
 * here as file-statics — every reader/writer is in this translation unit, so no
 * cross-file exposure is needed. web_ui.c drives setup via web_ui_bt_init().
 */
#include "web_ui.h"
#include "web_ui_internal.h"
#include "bt_link.h"
#include "ctrl.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_http_server.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

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

/* Console INFO capture: multi-line commands (HELP, PAIRED, ...) stream their
 * output as INFO| lines before the terminal OK|. While a console command runs
 * we buffer those so /api/console can return them to the Terminal. */
#define CONSOLE_MAX_LINES 40
#define CONSOLE_LINE_MAX  100
static char              s_console_lines[CONSOLE_MAX_LINES][CONSOLE_LINE_MAX];
static int               s_console_n;
static bool              s_console_capturing;

/* ---- /api/scan: suspend A2DP, run a BT inquiry, restore (CTRL) ---- */

esp_err_t scan_post_h(httpd_req_t *req)
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

esp_err_t btvolume_get_h(httpd_req_t *req)
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

esp_err_t btvolume_post_h(httpd_req_t *req)
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

esp_err_t ctrl_get_h(httpd_req_t *req)
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

esp_err_t ctrl_post_h(httpd_req_t *req)
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

/* Copy the CONN_MAC=<mac> value (the connected A2DP sink) out of a WROOM32
 * STATUS data string into out. Empty (out[0]='\0') when not connected. */
static void bt_status_conn_mac(const char *data, char *out, size_t out_sz)
{
    out[0] = '\0';
    const char *p = data;
    while ((p = strstr(p, "CONN_MAC=")) != NULL) {
        if (p == data || p[-1] == ',') {
            p += 9;                         /* past "CONN_MAC=" */
            size_t n = 0;
            while (p[n] && p[n] != ',' && n < out_sz - 1) n++;
            memcpy(out, p, n);
            out[n] = '\0';
            return;
        }
        p += 9;
    }
}

/* bt_link async-line subscriber -> buffer into the BT state. Runs on the
 * bt_link task; keep it quick and non-blocking. */
static void on_bt_event(void *ctx, const bt_link_msg_t *m)
{
    (void)ctx;
    if (!s_bt_mtx) return;
    char mac[20], name[36];
    xSemaphoreTake(s_bt_mtx, portMAX_DELAY);
    /* Buffer any INFO line while a console command is capturing (e.g. HELP
     * entries), so /api/console can return the full multi-line output. */
    if (s_console_capturing && m->status == BT_LINK_INFO && s_console_n < CONSOLE_MAX_LINES) {
        const char *txt = (m->data && m->data[0]) ? m->data : m->result;
        strlcpy(s_console_lines[s_console_n++], txt ? txt : "", CONSOLE_LINE_MAX);
    }
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
esp_err_t bt_get_h(httpd_req_t *req)
{
    bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;
    char sdata[BT_LINK_FIELD_MAX] = {0};
    bt_link_send("STATUS", &st, NULL, 0, sdata, sizeof(sdata));
    char conn_mac[20] = {0};
    if (st == BT_LINK_CMD_DONE_OK) bt_status_conn_mac(sdata, conn_mac, sizeof(conn_mac));
    /* A device is connected iff the WROOM32 reports a peer MAC. RUN alone is
     * unreliable — the audio engine keeps "running" (emitting silence) after a
     * disconnect, so RUN=1 can outlive the A2DP link. */
    bool connected = (st == BT_LINK_CMD_DONE_OK) && conn_mac[0] != '\0';

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "connected", connected);
    if (conn_mac[0]) cJSON_AddStringToObject(root, "connected_mac", conn_mac);
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

esp_err_t bt_post_h(httpd_req_t *req)
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
        if (xTaskCreate(connect_volume_task, "connvol", 4096, NULL,
                        tskIDLE_PRIORITY + 3, &s_conn_vol_task) != pdPASS) {
            ESP_LOGW(TAG, "failed to create connect-volume task");
        }
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
esp_err_t console_post_h(httpd_req_t *req)
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

    /* Capture any INFO lines streamed during the command (HELP, PAIRED, ...). */
    if (s_bt_mtx) {
        xSemaphoreTake(s_bt_mtx, portMAX_DELAY);
        s_console_n = 0;
        s_console_capturing = true;
        xSemaphoreGive(s_bt_mtx);
    }
    bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;
    char result[BT_LINK_FIELD_MAX] = {0}, data[BT_LINK_FIELD_MAX] = {0};
    bt_link_send(cmd, &st, result, sizeof(result), data, sizeof(data));
    const char *status = (st == BT_LINK_CMD_DONE_OK) ? "OK"
                       : (st == BT_LINK_CMD_DONE_ERR) ? "ERR" : "TIMEOUT";
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "status", status);
    cJSON_AddStringToObject(o, "result", result);
    cJSON_AddStringToObject(o, "data", data);
    /* Snapshot + emit the captured INFO lines (bt_link task is done appending
     * once the terminal response above has arrived). */
    cJSON *lines = cJSON_AddArrayToObject(o, "lines");
    if (s_bt_mtx) {
        xSemaphoreTake(s_bt_mtx, portMAX_DELAY);
        s_console_capturing = false;
        for (int i = 0; i < s_console_n; i++) {
            cJSON_AddItemToArray(lines, cJSON_CreateString(s_console_lines[i]));
        }
        xSemaphoreGive(s_bt_mtx);
    }
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    httpd_resp_sendstr(req, s ? s : "{\"status\":\"TIMEOUT\"}");
    if (s) cJSON_free(s);
    return ESP_OK;
}

/* Create the BT-state mutex, subscribe the bt_link async-line handler, and
 * prime the paired list. Called once by web_ui_start() after httpd is up. */
void web_ui_bt_init(void)
{
    s_bt_mtx = xSemaphoreCreateMutex();
    bt_link_subscribe(on_bt_event, NULL);
    bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;
    bt_link_send("PAIRED", &st, NULL, 0, NULL, 0);   /* items arrive via on_bt_event */
}
