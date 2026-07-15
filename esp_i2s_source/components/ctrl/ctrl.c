/* ctrl — boot orchestrator device glue (CTRL-1b). See ctrl.h. */
#include "ctrl.h"
#include "ctrl_sm.h"

#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "bt_link.h"
#include "wifi_mgr.h"
#include "radio.h"
#include "stations.h"
#include "esp_timer.h"

static const char *TAG = "ctrl";

#define CTRL_LOOP_MS 500

static ctrl_cfg_t        s_cfg;
static ctrl_sm_t         s_sm;
static TaskHandle_t      s_task;
static SemaphoreHandle_t s_mtx;
static TaskHandle_t      s_scan_task;
static _Atomic bool      s_scan_active;   /* pause the orchestrator during a scan */

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
                 vst == BT_LINK_CMD_TIMEOUT ? "timeout" :
                 vst == BT_LINK_CMD_DONE_OK ? "ok" : "fail");

        /* Resume the last station by stable ID. */
        if (s_cfg.last_station_id != CTRL_LAST_STATION_NONE) {
            /* Find the station by ID in the store. We iterate through all
             * stations to find one matching the ID. */
            char url[RADIO_URL_MAX];
            bool found = false;
            for (int i = 0; i < stations_count(); i++) {
                uint32_t sid;
                if (stations_get(i, NULL, 0, url, sizeof(url), &sid) &&
                    sid == s_cfg.last_station_id) {
                    found = true;
                    break;
                }
            }
            if (found) {
                ESP_LOGI(TAG, "resume station id=%u", s_cfg.last_station_id);
                esp_err_t err = radio_play_async(url);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "resume play failed: %s", esp_err_to_name(err));
                }
            } else {
                ESP_LOGW(TAG, "resume: station id=%u not found", s_cfg.last_station_id);
            }
        } else {
            ESP_LOGW(TAG, "resume: no station id set");
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
    bool have_station = (s_cfg.last_station_id != CTRL_LAST_STATION_NONE);
    ctrl_sm_init(&s_sm, autostart, have_station);

    if (!autostart) {
        ESP_LOGI(TAG, "autostart off (flag=%u mac=%s) -> manual mode",
                 s_cfg.autostart, mac_ok ? s_cfg.sink_mac : "(unset/invalid)");
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "orchestrating: sink=%s last_station_id=%u",
             s_cfg.sink_mac, s_cfg.last_station_id);

    int64_t last_us = 0;
    for (;;) {
        /* A scan intentionally drops A2DP for the inquiry — don't fight it by
         * health-poll-reconnecting. scan_task restores the link when done. */
        if (s_scan_active) {
            vTaskDelay(pdMS_TO_TICKS(CTRL_LOOP_MS));
            continue;
        }

        /* Monotonic timestamp for dt_ms */
        int64_t now_us = esp_timer_get_time();
        uint32_t dt_ms = (last_us == 0) ? CTRL_LOOP_MS :
                         (uint32_t)((now_us - last_us) / 1000);
        last_us = now_us;

        ctrl_input_t in = {0};
        if (s_sm.state == CTRL_ST_WAIT_WIFI && wifi_connected()) {
            in.ev = CTRL_EV_WIFI_UP;
        } else {
            in.ev = CTRL_EV_TICK;
            in.dt_ms = dt_ms;
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

/* Suspend A2DP for a clean classic-BT inquiry, then restore. Classic inquiry
 * is unreliable while an A2DP link is active (they share one radio), so stop
 * the stream, disconnect the sink, run SCAN, then reconnect + resume.
 * Discovery results fan out over the WS as INFO|SCAN|RESULT during the inquiry
 * window. */
#define SCAN_INQUIRY_MS  15000
#define SCAN_SETTLE_MS    4000
#define SCAN_RADIO_TIMEOUT_MS 5000  /* timeout for radio stop/start polling */

/* Scan state machine phases */
typedef enum {
    SCAN_SUSPENDING,   /* stop radio, wait for stopped */
    SCAN_DISCONNECT,   /* disconnect sink */
    SCAN_INQUIRY,      /* run SCAN inquiry */
    SCAN_RESTORE,      /* reconnect sink */
    SCAN_VOLUME,       /* re-apply volume */
    SCAN_RESUME,       /* resume radio, wait for starting/running */
    SCAN_DONE          /* terminal — cleanup and exit */
} scan_phase_t;

/* Poll until the radio reports `target_state`, or until `deadline_us` expires.
 * Returns true if the deadline expired without reaching the target state. */
static bool scan_wait_for_state(radio_state_t target_state, int64_t deadline_us,
                                const char *_phase)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        radio_state_t state = radio_get_state();
        if (state == target_state) return false;  /* reached target */
        if (esp_timer_get_time() >= deadline_us) break;
    }
    ESP_LOGW(TAG, "scan: %s timed out", _phase);
    (void)_phase;
    return true;
}

/* Poll until the radio is in any STARTING/RUNNING state, or until deadline.
 * Returns true if the deadline expired. */
static bool scan_wait_for_radio_start(int64_t deadline_us, const char *_phase)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        radio_state_t state = radio_get_state();
        if (state == RADIO_STATE_STARTING || state == RADIO_STATE_RUNNING)
            return false;  /* started */
        if (esp_timer_get_time() >= deadline_us) break;
    }
    ESP_LOGW(TAG, "scan: %s timed out", _phase);
    (void)_phase;
    return true;
}

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

    scan_phase_t phase = SCAN_SUSPENDING;
    bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;

    while (phase != SCAN_DONE) {
        switch (phase) {

        case SCAN_SUSPENDING:
            /* Stop the radio and poll until it reports STOPPED. */
            if (was_playing) {
                esp_err_t e = radio_stop_async();
                if (e != ESP_OK) {
                    ESP_LOGW(TAG, "scan: radio_stop_async failed: %s", esp_err_to_name(e));
                }
            }

            if (was_playing) {
                int64_t deadline = esp_timer_get_time() +
                    (int64_t)SCAN_RADIO_TIMEOUT_MS * 1000;
                bool timed_out = scan_wait_for_state(RADIO_STATE_STOPPED, deadline,
                                                     "suspend");
                if (timed_out) {
                    ESP_LOGW(TAG, "scan: radio still not stopped after timeout");
                }
            }
            phase = SCAN_DISCONNECT;
            break;

        case SCAN_DISCONNECT:
            /* Drop the sink so the classic BT radio is free for inquiry. */
            ESP_LOGI(TAG, "scan: suspending A2DP (disconnect sink)");
            bt_link_send("DISCONNECT", &st, NULL, 0, NULL, 0);
            vTaskDelay(pdMS_TO_TICKS(1500));
            phase = SCAN_INQUIRY;
            break;

        case SCAN_INQUIRY:
            /* Inquiry — web_ui's bt_link subscription fans INFO|SCAN|RESULT
             * to connected clients during the inquiry window. */
            ESP_LOGI(TAG, "scan: starting inquiry");
            bt_link_send("SCAN", &st, NULL, 0, NULL, 0);
            vTaskDelay(pdMS_TO_TICKS(SCAN_INQUIRY_MS));
            phase = SCAN_RESTORE;
            break;

        case SCAN_RESTORE:
            /* Reconnect the sink. */
            if (have_sink) {
                char cmd[16 + CTRL_MAC_LEN];
                snprintf(cmd, sizeof(cmd), "CONNECT %s", cfg.sink_mac);
                bt_link_send(cmd, &st, NULL, 0, NULL, 0);
                vTaskDelay(pdMS_TO_TICKS(SCAN_SETTLE_MS));
            }
            phase = SCAN_VOLUME;
            break;

        case SCAN_VOLUME:
            /* Re-apply volume — the WROOM32 resets VOL to 40 on a fresh
             * A2DP link, so re-assert the user's configured level. */
            if (have_sink) {
                char cmd[16];
                snprintf(cmd, sizeof(cmd), "VOLUME %u", cfg.volume);
                bt_link_send(cmd, &st, NULL, 0, NULL, 0);
            }
            phase = SCAN_RESUME;
            break;

        case SCAN_RESUME:
            /* Resume the radio if it was playing before the scan. */
            if (was_playing && url[0]) {
                ESP_LOGI(TAG, "scan: resuming radio");
                esp_err_t e = radio_play_async(url);
                if (e != ESP_OK) {
                    ESP_LOGW(TAG, "scan: radio_play_async failed: %s", esp_err_to_name(e));
                } else {
                    /* Wait for radio to actually start streaming. */
                    int64_t deadline = esp_timer_get_time() +
                        (int64_t)SCAN_RADIO_TIMEOUT_MS * 1000;
                    bool timed_out = scan_wait_for_radio_start(deadline, "resume");
                    if (timed_out) {
                        ESP_LOGW(TAG, "scan: radio did not start after timeout");
                    }
                }
            }
            phase = SCAN_DONE;
            break;

        case SCAN_DONE:
            ESP_LOGI(TAG, "scan: done, A2DP restored");
            break;
        }
    }

    s_scan_active = false;
    s_scan_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t ctrl_scan(void)
{
    if (!s_mtx) return ESP_ERR_INVALID_STATE;  /* RH-S3-10: defensive check */
    if (s_scan_task) return ESP_ERR_INVALID_STATE;   /* already scanning */
    if (xTaskCreate(scan_task, "ctrl_scan", 4096, NULL,
                    tskIDLE_PRIORITY + 3, &s_scan_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool ctrl_scan_active(void)
{
    return atomic_load(&s_scan_active);
}

/* RH-S3-10: split initialisation from start so mutex exists before web UI. */
esp_err_t ctrl_init(void)
{
    if (s_mtx) return ESP_OK;  /* already initialised — idempotent */
    s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) return ESP_ERR_NO_MEM;
    /* Zero config — ctrl_cfg_load fills it in below. */
    memset(&s_cfg, 0, sizeof(s_cfg));
    ctrl_cfg_load(&s_cfg);
    return ESP_OK;
}

esp_err_t ctrl_start(void)
{
    /* RH-S3-10: require ctrl_init() to have been called first. */
    if (!s_mtx) return ESP_ERR_INVALID_STATE;

    /* Pass a copied initial config to the task — the task reads s_cfg
     * synchronously under the mutex, or stores its own copy. */
    ctrl_cfg_t initial_cfg;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    initial_cfg = s_cfg;
    xSemaphoreGive(s_mtx);

    /* Store the initial config in a task-local variable */
    s_cfg = initial_cfg;

    if (xTaskCreate(orchestrator_task, "ctrl", 4096, NULL,
                    tskIDLE_PRIORITY + 3, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void ctrl_get_cfg(ctrl_cfg_t *out)
{
    if (!out) return;
    if (!s_mtx) return;  /* RH-S3-10: defensive — not initialised yet */
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *out = s_cfg;
    xSemaphoreGive(s_mtx);
}

esp_err_t ctrl_set_sink(const char *mac, bool autostart, int volume)
{
    if (!s_mtx) return ESP_ERR_INVALID_STATE;  /* RH-S3-10: defensive check */
    if (mac && mac[0] && !ctrl_cfg_mac_valid(mac)) return ESP_ERR_INVALID_ARG;

    /* Candidate pattern: build under lock, save, publish. */
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    ctrl_cfg_t candidate = s_cfg;
    if (mac) strlcpy(candidate.sink_mac, mac, sizeof(candidate.sink_mac));
    candidate.autostart = autostart ? 1 : 0;
    if (volume >= 0) {
        candidate.volume = (volume > 100) ? 100 : (uint8_t)volume;
    }
    s_cfg = candidate;
    esp_err_t e = ctrl_cfg_save(&s_cfg);
    xSemaphoreGive(s_mtx);

    /* Log from candidate — s_cfg may have changed since the mutex was released. */
    ESP_LOGI(TAG, "config set: sink=%s autostart=%u volume=%u (%s)",
             candidate.sink_mac, candidate.autostart, candidate.volume, esp_err_to_name(e));
    return e;
}

esp_err_t ctrl_note_station(uint32_t station_id)
{
    if (!s_mtx) return ESP_ERR_INVALID_STATE;  /* RH-S3-10: defensive check */
    esp_err_t err = ESP_OK;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (station_id != s_cfg.last_station_id) {
        s_cfg.last_station_id = station_id;
        err = ctrl_cfg_save(&s_cfg);
    }
    xSemaphoreGive(s_mtx);
    return err;
}