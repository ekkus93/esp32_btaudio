/* ctrl — boot orchestrator device glue (CTRL-1b). See ctrl.h. */
#include "ctrl.h"
#include "ctrl_sm.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "bt_link.h"
#include "wifi_mgr.h"
#include "radio.h"
#include "stations.h"

static const char *TAG = "ctrl";

#define CTRL_LOOP_MS 500

static ctrl_cfg_t        s_cfg;
static ctrl_sm_t         s_sm;
static TaskHandle_t      s_task;
static SemaphoreHandle_t s_mtx;
static TaskHandle_t      s_scan_task;
static volatile bool     s_scan_active;   /* pause the orchestrator during a scan */

static bool wifi_connected(void)
{
    wifi_mgr_info_t wi;
    wifi_mgr_get_info(&wi);
    return strcmp(wi.state, "CONNECTED") == 0;
}

/* Parse the WROOM32 STATUS data ("...,RUN=1,...") for the streaming flag. Only
 * the standalone RUN= token counts (skips UNDERRUN_RATE=, UNDERRUNS=). */
static bool status_running(const char *data)
{
    const char *p = data;
    while ((p = strstr(p, "RUN=")) != NULL) {
        if (p == data || p[-1] == ',') return p[4] == '1';
        p += 4;
    }
    return false;
}

/* Execute one FSM action against the hardware and feed the result back into the
 * machine, returning the next action. */
static ctrl_action_t do_action(ctrl_action_t act)
{
    ctrl_input_t in = {0};
    bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;

    switch (act) {
    case CTRL_ACT_SEND_STATUS: {
        char data[BT_LINK_FIELD_MAX] = {0};
        bt_link_send("STATUS", &st, NULL, 0, data, sizeof(data));
        in.ev = CTRL_EV_STATUS;
        in.connected = (st == BT_LINK_CMD_DONE_OK) && status_running(data);
        break;
    }
    case CTRL_ACT_SEND_CONNECT: {
        char cmd[16 + CTRL_MAC_LEN];
        snprintf(cmd, sizeof(cmd), "CONNECT %s", s_cfg.sink_mac);
        bt_link_send(cmd, &st, NULL, 0, NULL, 0);
        in.ev = CTRL_EV_CONNECT_ACK;
        in.ok = (st == BT_LINK_CMD_DONE_OK);
        ESP_LOGI(TAG, "CONNECT %s -> %s", s_cfg.sink_mac, in.ok ? "initiated" : "fail");
        break;
    }
    case CTRL_ACT_SEND_START:
        bt_link_send("START", &st, NULL, 0, NULL, 0);
        in.ev = CTRL_EV_START_ACK;
        in.ok = (st == BT_LINK_CMD_DONE_OK);
        ESP_LOGI(TAG, "START -> %s", in.ok ? "started" : "fail");
        break;
    case CTRL_ACT_RESUME_RADIO: {
        /* Re-assert the configured volume BEFORE audio resumes — the WROOM32
         * resets VOL to 40 on a fresh A2DP link, so without this autostart music
         * would come up loud rather than at the user's level. */
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "VOLUME %u", s_cfg.volume);
        bt_link_cmd_state_t vst = BT_LINK_CMD_TIMEOUT;
        bt_link_send(cmd, &vst, NULL, 0, NULL, 0);
        ESP_LOGI(TAG, "set volume %u -> %s", s_cfg.volume,
                 vst == BT_LINK_CMD_DONE_OK ? "ok" : "fail");

        char url[RADIO_URL_MAX];
        int idx = s_cfg.last_station;
        if (stations_get_url(idx, url, sizeof(url))) {
            ESP_LOGI(TAG, "resume station %d", idx);
            esp_err_t err = radio_play(url);   /* blocking; fine on this task */
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "resume play failed: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGW(TAG, "resume: station %d unavailable", idx);
        }
        in.ev = CTRL_EV_RESUME_DONE;
        break;
    }
    default:
        return CTRL_ACT_WAIT;
    }
    return ctrl_sm_step(&s_sm, &in);
}

static void orchestrator_task(void *arg)
{
    (void)arg;
    bool mac_ok = ctrl_cfg_mac_valid(s_cfg.sink_mac);
    bool autostart = s_cfg.autostart && mac_ok;
    ctrl_sm_init(&s_sm, autostart, s_cfg.last_station >= 0);

    if (!autostart) {
        ESP_LOGI(TAG, "autostart off (flag=%u mac=%s) -> manual mode",
                 s_cfg.autostart, mac_ok ? s_cfg.sink_mac : "(unset/invalid)");
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "orchestrating: sink=%s resume_station=%d",
             s_cfg.sink_mac, s_cfg.last_station);

    for (;;) {
        /* A scan intentionally drops A2DP for the inquiry — don't fight it by
         * health-poll-reconnecting. scan_task restores the link when done. */
        if (s_scan_active) {
            vTaskDelay(pdMS_TO_TICKS(CTRL_LOOP_MS));
            continue;
        }
        ctrl_input_t in = {0};
        if (s_sm.state == CTRL_ST_WAIT_WIFI && wifi_connected()) {
            in.ev = CTRL_EV_WIFI_UP;
        } else {
            in.ev = CTRL_EV_TICK;
            in.dt_ms = CTRL_LOOP_MS;
        }
        ctrl_action_t act = ctrl_sm_step(&s_sm, &in);
        while (act != CTRL_ACT_WAIT && act != CTRL_ACT_IDLE) {
            act = do_action(act);
        }
        if (s_sm.state == CTRL_ST_IDLE || s_sm.state == CTRL_ST_GAVEUP) {
            ESP_LOGW(TAG, "orchestrator done: %s", ctrl_state_str(s_sm.state));
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(CTRL_LOOP_MS));
    }
    s_task = NULL;
    vTaskDelete(NULL);
}

/* Suspend A2DP for a clean classic-BT inquiry, then restore. Classic inquiry is
 * unreliable while an A2DP link is active (they share one radio), so stop the
 * stream, disconnect the sink, run SCAN, then reconnect + resume. Discovery
 * results fan out over the WS as INFO|SCAN|RESULT during the inquiry window. */
#define SCAN_INQUIRY_MS  15000
#define SCAN_SETTLE_MS    4000

static void scan_task(void *arg)
{
    (void)arg;
    /* Snapshot what to restore afterward. */
    radio_status_t rs;
    radio_get_status(&rs);
    bool was_playing = rs.playing;
    char url[RADIO_URL_MAX];
    strlcpy(url, rs.url, sizeof(url));
    ctrl_cfg_t cfg;
    ctrl_get_cfg(&cfg);
    bool have_sink = ctrl_cfg_mac_valid(cfg.sink_mac);

    s_scan_active = true;
    bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;

    /* Suspend A2DP: stop feeding I2S, drop the sink so the radio is free. */
    if (was_playing) {
        esp_err_t e = radio_stop();
        if (e != ESP_OK) ESP_LOGW(TAG, "scan: radio_stop failed: %s", esp_err_to_name(e));
    }
    ESP_LOGI(TAG, "scan: suspending A2DP (disconnect sink)");
    bt_link_send("DISCONNECT", &st, NULL, 0, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(1500));

    /* Inquiry — web_ui's bt_link subscription fans INFO|SCAN|RESULT to clients. */
    ESP_LOGI(TAG, "scan: starting inquiry");
    bt_link_send("SCAN", &st, NULL, 0, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(SCAN_INQUIRY_MS));

    /* Restore: reconnect the sink, re-apply volume, resume the station. */
    if (have_sink) {
        char cmd[16 + CTRL_MAC_LEN];
        snprintf(cmd, sizeof(cmd), "CONNECT %s", cfg.sink_mac);
        bt_link_send(cmd, &st, NULL, 0, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(SCAN_SETTLE_MS));
        snprintf(cmd, sizeof(cmd), "VOLUME %u", cfg.volume);
        bt_link_send(cmd, &st, NULL, 0, NULL, 0);
    }
    if (was_playing && url[0]) {
        ESP_LOGI(TAG, "scan: resuming radio");
        radio_play(url);
    }
    ESP_LOGI(TAG, "scan: done, A2DP restored");
    s_scan_active = false;
    s_scan_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t ctrl_scan(void)
{
    if (s_scan_task) return ESP_ERR_INVALID_STATE;   /* already scanning */
    if (xTaskCreate(scan_task, "ctrl_scan", 4096, NULL,
                    tskIDLE_PRIORITY + 3, &s_scan_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool ctrl_scan_active(void)
{
    return s_scan_active;
}

esp_err_t ctrl_start(void)
{
    if (!s_mtx) {
        s_mtx = xSemaphoreCreateMutex();
        if (!s_mtx) return ESP_ERR_NO_MEM;
    }
    ctrl_cfg_load(&s_cfg);
    if (xTaskCreate(orchestrator_task, "ctrl", 4096, NULL,
                    tskIDLE_PRIORITY + 3, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void ctrl_get_cfg(ctrl_cfg_t *out)
{
    if (!out) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *out = s_cfg;
    xSemaphoreGive(s_mtx);
}

esp_err_t ctrl_set_sink(const char *mac, bool autostart, int volume)
{
    if (mac && mac[0] && !ctrl_cfg_mac_valid(mac)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (mac) strlcpy(s_cfg.sink_mac, mac, sizeof(s_cfg.sink_mac));
    s_cfg.autostart = autostart ? 1 : 0;
    if (volume >= 0) {
        s_cfg.volume = (volume > 100) ? 100 : (uint8_t)volume;
    }
    esp_err_t e = ctrl_cfg_save(&s_cfg);
    xSemaphoreGive(s_mtx);
    ESP_LOGI(TAG, "config set: sink=%s autostart=%u volume=%u (%s)",
             s_cfg.sink_mac, s_cfg.autostart, s_cfg.volume, esp_err_to_name(e));
    return e;
}

void ctrl_note_station(int idx)
{
    if (!s_mtx) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (idx != s_cfg.last_station) {
        s_cfg.last_station = (int16_t)idx;
        ctrl_cfg_save(&s_cfg);
    }
    xSemaphoreGive(s_mtx);
}
