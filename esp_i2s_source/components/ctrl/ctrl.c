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
/* 9.3: dedicated update mutex — held across the full snapshot -> validate ->
 * persist -> publish sequence of ctrl_set_sink()/ctrl_note_station(), so two
 * concurrent setters can never interleave and lose one's update. s_mtx alone
 * only guards the (short) in-memory s_cfg read/write, not the whole
 * transaction. */
static SemaphoreHandle_t s_update_mtx;
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

/* Truthful resume outcome (FIX3 9.5) — RESUME_DONE is only emitted once every
 * step below actually succeeds; any other outcome is RESUME_FAILED with a
 * distinct, loggable reason. */
typedef enum {
    CTRL_RESUME_OK = 0,
    CTRL_RESUME_VOLUME_FAILED,
    CTRL_RESUME_NO_STATION,
    CTRL_RESUME_STATION_NOT_FOUND,
    CTRL_RESUME_PLAY_ENQUEUE_FAILED,
} ctrl_resume_result_t;

static const char *resume_result_str(ctrl_resume_result_t r)
{
    switch (r) {
    case CTRL_RESUME_OK:                   return "OK";
    case CTRL_RESUME_VOLUME_FAILED:        return "VOLUME_FAILED";
    case CTRL_RESUME_NO_STATION:            return "NO_STATION";
    case CTRL_RESUME_STATION_NOT_FOUND:     return "STATION_NOT_FOUND";
    case CTRL_RESUME_PLAY_ENQUEUE_FAILED:   return "PLAY_ENQUEUE_FAILED";
    default:                                return "?";
    }
}

/* Execute one FSM action against the hardware and feed the result back into
 * the machine, returning the next action. `cfg` is an immutable snapshot for
 * this one action chain — 9.1: never read the mutable s_cfg here, so a
 * concurrent ctrl_set_sink()/ctrl_note_station() can't hand this call a
 * torn view partway through an attempt. */
static ctrl_action_t do_action(ctrl_action_t act, const ctrl_cfg_t *cfg)
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
        snprintf(cmd, sizeof(cmd), "CONNECT %s", cfg->sink_mac);
        bt_link_send(cmd, &st, NULL, 0, NULL, 0);
        in.ev = CTRL_EV_CONNECT_ACK;
        in.ok = (st == BT_LINK_CMD_DONE_OK);
        ESP_LOGI(TAG, "CONNECT %s -> %s", cfg->sink_mac, in.ok ? "initiated" : "fail");
        break;
    }
    case CTRL_ACT_SEND_START:
        bt_link_send("START", &st, NULL, 0, NULL, 0);
        in.ev = CTRL_EV_START_ACK;
        in.ok = (st == BT_LINK_CMD_DONE_OK);
        ESP_LOGI(TAG, "START -> %s", in.ok ? "started" : "fail");
        break;
    case CTRL_ACT_RESUME_RADIO: {
        ctrl_resume_result_t result = CTRL_RESUME_OK;

        /* 1-2: re-assert the configured volume BEFORE audio resumes (the
         * WROOM32 resets VOL to 40 on a fresh A2DP link) and require both
         * transport success and DONE_OK — a timeout/error here must not be
         * silently treated as "volume applied". */
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "VOLUME %u", cfg->volume);
        bt_link_cmd_state_t vst = BT_LINK_CMD_TIMEOUT;
        esp_err_t vol_err = bt_link_send(cmd, &vst, NULL, 0, NULL, 0);
        ESP_LOGI(TAG, "set volume %u -> %s", cfg->volume,
                 vst == BT_LINK_CMD_TIMEOUT ? "timeout" :
                 vst == BT_LINK_CMD_DONE_OK ? "ok" : "fail");
        if (vol_err != ESP_OK || vst != BT_LINK_CMD_DONE_OK) {
            result = CTRL_RESUME_VOLUME_FAILED;
        }

        /* 3-5: resolve the station, require it found, enqueue play. */
        if (result == CTRL_RESUME_OK) {
            if (cfg->last_station_id == CTRL_LAST_STATION_NONE) {
                ESP_LOGW(TAG, "resume: no station id set");
                result = CTRL_RESUME_NO_STATION;
            } else {
                char url[RADIO_URL_MAX];
                bool found = false;
                for (int i = 0; i < stations_count(); i++) {
                    uint32_t sid;
                    if (stations_get(i, NULL, 0, url, sizeof(url), &sid) &&
                        sid == cfg->last_station_id) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ESP_LOGW(TAG, "resume: station id=%u not found", cfg->last_station_id);
                    result = CTRL_RESUME_STATION_NOT_FOUND;
                } else {
                    ESP_LOGI(TAG, "resume station id=%u", cfg->last_station_id);
                    esp_err_t err = radio_play_async(url);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "resume play failed: %s", esp_err_to_name(err));
                        result = CTRL_RESUME_PLAY_ENQUEUE_FAILED;
                    }
                }
            }
        }

        /* 6: only now emit RESUME_DONE — any other outcome is RESUME_FAILED,
         * with the reason visible in the diagnostic (not silently equated
         * with success). */
        if (result == CTRL_RESUME_OK) {
            in.ev = CTRL_EV_RESUME_DONE;
        } else {
            in.ev = CTRL_EV_RESUME_FAILED;
            printf("DIAG|CTRL|RESUME_FAILED|reason=%s\n", resume_result_str(result));
            fflush(stdout);
        }
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
    ctrl_cfg_t cfg;
    ctrl_get_cfg(&cfg);
    bool mac_ok = ctrl_cfg_mac_valid(cfg.sink_mac);
    bool autostart = cfg.autostart && mac_ok;
    bool have_station = (cfg.last_station_id != CTRL_LAST_STATION_NONE);
    ctrl_sm_init(&s_sm, autostart, have_station);

    if (!autostart) {
        ESP_LOGI(TAG, "autostart off (flag=%u mac=%s) -> manual mode",
                 cfg.autostart, mac_ok ? cfg.sink_mac : "(unset/invalid)");
        /* 9.2: clear the handle under the same mutex ctrl_start() checks it
         * with — set/clear-under-mutex is the documented safe pattern here
         * (the task never runs concurrently with its own exit). */
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_task = NULL;
        xSemaphoreGive(s_mtx);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "orchestrating: sink=%s last_station_id=%u",
             cfg.sink_mac, cfg.last_station_id);

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

        /* 9.1: a fresh, immutable snapshot for this tick's action chain —
         * config changes from a concurrent ctrl_set_sink()/ctrl_note_station()
         * apply starting next tick, never mid-attempt. */
        ctrl_get_cfg(&cfg);

        ctrl_input_t in = {0};
        if (s_sm.state == CTRL_ST_WAIT_WIFI && wifi_connected()) {
            in.ev = CTRL_EV_WIFI_UP;
        } else {
            in.ev = CTRL_EV_TICK;
            in.dt_ms = dt_ms;
        }
        ctrl_action_t act = ctrl_sm_step(&s_sm, &in);
        while (act != CTRL_ACT_WAIT && act != CTRL_ACT_IDLE) {
            act = do_action(act, &cfg);
        }
        if (s_sm.state == CTRL_ST_IDLE || s_sm.state == CTRL_ST_GAVEUP) {
            ESP_LOGW(TAG, "orchestrator done: %s", ctrl_state_str(s_sm.state));
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(CTRL_LOOP_MS));
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_task = NULL;
    xSemaphoreGive(s_mtx);
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
 * Returns true if the deadline expired (or the radio faulted) without
 * reaching the target state. 9.7: FAULTED/FAULTED_JOIN_PENDING terminate the
 * wait immediately as a failure — no evidence more waiting will help. */
static bool scan_wait_for_state(radio_state_t target_state, int64_t deadline_us,
                                const char *_phase)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        radio_state_t state = radio_get_state();
        if (state == target_state) return false;  /* reached target */
        if (state == RADIO_STATE_FAULTED || state == RADIO_STATE_FAULTED_JOIN_PENDING) {
            ESP_LOGW(TAG, "scan: %s aborted (radio FAULTED)", _phase);
            return true;
        }
        if (esp_timer_get_time() >= deadline_us) break;
    }
    ESP_LOGW(TAG, "scan: %s timed out", _phase);
    (void)_phase;
    return true;
}

/* Poll until the radio reaches BUFFERING or RUNNING (9.7: STARTING alone is
 * not evidence of operational startup), or until deadline/fault. Returns
 * true if the deadline expired or the radio faulted. */
static bool scan_wait_for_radio_start(int64_t deadline_us, const char *_phase)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        radio_state_t state = radio_get_state();
        if (state == RADIO_STATE_BUFFERING || state == RADIO_STATE_RUNNING)
            return false;  /* started */
        if (state == RADIO_STATE_FAULTED || state == RADIO_STATE_FAULTED_JOIN_PENDING) {
            ESP_LOGW(TAG, "scan: %s aborted (radio FAULTED)", _phase);
            return true;
        }
        if (esp_timer_get_time() >= deadline_us) break;
    }
    ESP_LOGW(TAG, "scan: %s timed out", _phase);
    (void)_phase;
    return true;
}

/* 9.6: explicit scan outcome — stored so a caller/diagnostic can tell exactly
 * which phase failed, rather than inferring it from log lines. */
typedef enum {
    CTRL_SCAN_OK = 0,
    CTRL_SCAN_RADIO_STOP_FAILED,
    CTRL_SCAN_DISCONNECT_FAILED,
    CTRL_SCAN_COMMAND_FAILED,
    CTRL_SCAN_RECONNECT_FAILED,
    CTRL_SCAN_VOLUME_FAILED,
    CTRL_SCAN_RADIO_RESUME_FAILED,
} ctrl_scan_result_t;

static const char *scan_result_str(ctrl_scan_result_t r)
{
    switch (r) {
    case CTRL_SCAN_OK:                    return "OK";
    case CTRL_SCAN_RADIO_STOP_FAILED:     return "RADIO_STOP_FAILED";
    case CTRL_SCAN_DISCONNECT_FAILED:     return "DISCONNECT_FAILED";
    case CTRL_SCAN_COMMAND_FAILED:        return "COMMAND_FAILED";
    case CTRL_SCAN_RECONNECT_FAILED:      return "RECONNECT_FAILED";
    case CTRL_SCAN_VOLUME_FAILED:         return "VOLUME_FAILED";
    case CTRL_SCAN_RADIO_RESUME_FAILED:   return "RADIO_RESUME_FAILED";
    default:                              return "?";
    }
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

    ctrl_scan_result_t result = CTRL_SCAN_OK;
    /* 9.6: rollback tracks only what was ACTUALLY changed, so restoration
     * doesn't attempt to "undo" a step that never ran/succeeded. Trivially
     * true when there was nothing to do for that step. */
    bool radio_stopped     = !was_playing;
    bool sink_disconnected = false;
    bool sink_reconnected  = false;
    bool volume_restored   = false;
    bool radio_resumed     = !was_playing || !url[0];

    scan_phase_t phase = SCAN_SUSPENDING;
    bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;

    while (phase != SCAN_DONE) {
        switch (phase) {

        case SCAN_SUSPENDING:
            /* Stop the radio and poll until it reports STOPPED. Do not
             * continue to disconnect/inquiry if this fails (9.6). */
            if (was_playing) {
                esp_err_t e = radio_stop_async();
                if (e != ESP_OK) {
                    ESP_LOGW(TAG, "scan: radio_stop_async failed: %s", esp_err_to_name(e));
                    result = CTRL_SCAN_RADIO_STOP_FAILED;
                    phase = SCAN_DONE;
                    break;
                }
                int64_t deadline = esp_timer_get_time() +
                    (int64_t)SCAN_RADIO_TIMEOUT_MS * 1000;
                bool timed_out = scan_wait_for_state(RADIO_STATE_STOPPED, deadline, "suspend");
                if (timed_out) {
                    ESP_LOGW(TAG, "scan: radio still not stopped after timeout");
                    result = CTRL_SCAN_RADIO_STOP_FAILED;
                    phase = SCAN_DONE;
                    break;
                }
                radio_stopped = true;
            }
            phase = SCAN_DISCONNECT;
            break;

        case SCAN_DISCONNECT: {
            /* Drop the sink so the classic BT radio is free for inquiry.
             * Check both transport and command state (9.6). */
            ESP_LOGI(TAG, "scan: suspending A2DP (disconnect sink)");
            esp_err_t e = bt_link_send("DISCONNECT", &st, NULL, 0, NULL, 0);
            if (e != ESP_OK || st != BT_LINK_CMD_DONE_OK) {
                ESP_LOGW(TAG, "scan: DISCONNECT failed");
                result = CTRL_SCAN_DISCONNECT_FAILED;
                /* Nothing was actually disconnected -- go straight to
                 * restore/resume rollback rather than a fake inquiry. */
                phase = SCAN_RESTORE;
                break;
            }
            sink_disconnected = true;
            vTaskDelay(pdMS_TO_TICKS(1500));
            phase = SCAN_INQUIRY;
            break;
        }

        case SCAN_INQUIRY: {
            /* Inquiry — web_ui's bt_link subscription fans INFO|SCAN|RESULT
             * to connected clients during the inquiry window. If the SCAN
             * command itself fails, don't sleep 15s pretending inquiry is
             * active (9.6) -- go straight to restore. */
            ESP_LOGI(TAG, "scan: starting inquiry");
            esp_err_t e = bt_link_send("SCAN", &st, NULL, 0, NULL, 0);
            if (e != ESP_OK || st != BT_LINK_CMD_DONE_OK) {
                ESP_LOGW(TAG, "scan: SCAN command failed -- skipping inquiry wait");
                result = CTRL_SCAN_COMMAND_FAILED;
                phase = SCAN_RESTORE;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(SCAN_INQUIRY_MS));
            phase = SCAN_RESTORE;
            break;
        }

        case SCAN_RESTORE:
            /* Reconnect the sink (attempted whenever we got this far,
             * regardless of which earlier phase failed, since the sink may
             * still be disconnected from a prior successful DISCONNECT). */
            if (have_sink) {
                char cmd[16 + CTRL_MAC_LEN];
                snprintf(cmd, sizeof(cmd), "CONNECT %s", cfg.sink_mac);
                esp_err_t e = bt_link_send(cmd, &st, NULL, 0, NULL, 0);
                if (e != ESP_OK || st != BT_LINK_CMD_DONE_OK) {
                    ESP_LOGW(TAG, "scan: reconnect failed");
                    if (result == CTRL_SCAN_OK) result = CTRL_SCAN_RECONNECT_FAILED;
                    phase = SCAN_DONE;
                    break;
                }
                sink_reconnected = true;
                vTaskDelay(pdMS_TO_TICKS(SCAN_SETTLE_MS));
            } else {
                sink_reconnected = true;   /* nothing to reconnect */
            }
            phase = SCAN_VOLUME;
            break;

        case SCAN_VOLUME:
            /* Re-apply volume — the WROOM32 resets VOL to 40 on a fresh
             * A2DP link, so re-assert the user's configured level. A volume
             * failure is a partial-restore issue, not fatal to the rest of
             * recovery (9.6: "reports partial restore failure"). */
            if (have_sink) {
                char cmd[16];
                snprintf(cmd, sizeof(cmd), "VOLUME %u", cfg.volume);
                esp_err_t e = bt_link_send(cmd, &st, NULL, 0, NULL, 0);
                if (e != ESP_OK || st != BT_LINK_CMD_DONE_OK) {
                    ESP_LOGW(TAG, "scan: volume restore failed");
                    if (result == CTRL_SCAN_OK) result = CTRL_SCAN_VOLUME_FAILED;
                } else {
                    volume_restored = true;
                }
            } else {
                volume_restored = true;
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
                    if (result == CTRL_SCAN_OK) result = CTRL_SCAN_RADIO_RESUME_FAILED;
                } else {
                    int64_t deadline = esp_timer_get_time() +
                        (int64_t)SCAN_RADIO_TIMEOUT_MS * 1000;
                    bool timed_out = scan_wait_for_radio_start(deadline, "resume");
                    if (timed_out) {
                        ESP_LOGW(TAG, "scan: radio did not start after timeout");
                        if (result == CTRL_SCAN_OK) result = CTRL_SCAN_RADIO_RESUME_FAILED;
                    } else {
                        radio_resumed = true;
                    }
                }
            }
            phase = SCAN_DONE;
            break;

        case SCAN_DONE:
            break;
        }
    }

    /* 9.6: truthful final marker — "restored" means the things that
     * actually needed restoring (the sink connection, and playback if it
     * was playing) both succeeded; volume is tracked but doesn't gate this,
     * matching its "partial restore" status. */
    bool restored = (!have_sink || sink_reconnected) && (!was_playing || radio_resumed);

    printf("DIAG|CTRL|SCAN_DONE|restored=%d,result=%s,radio_stopped=%d,"
           "sink_disconnected=%d,sink_reconnected=%d,volume_restored=%d,radio_resumed=%d\n",
           restored ? 1 : 0, scan_result_str(result),
           radio_stopped, sink_disconnected, sink_reconnected, volume_restored, radio_resumed);
    fflush(stdout);
    if (restored) {
        ESP_LOGI(TAG, "scan: done, A2DP restored");
    } else {
        ESP_LOGW(TAG, "scan: done, A2DP NOT fully restored (result=%s)", scan_result_str(result));
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
    s_update_mtx = xSemaphoreCreateMutex();
    if (!s_update_mtx) {
        vSemaphoreDelete(s_mtx);
        s_mtx = NULL;
        return ESP_ERR_NO_MEM;
    }

    /* Zero config — ctrl_cfg_load fills it in below. */
    memset(&s_cfg, 0, sizeof(s_cfg));
    bool needs_legacy_resolve = false;
    int16_t legacy_index = CTRL_STATION_NONE;
    ctrl_cfg_load(&s_cfg, &needs_legacy_resolve, &legacy_index);

    /* 9.4: coordinator — ctrl_init() runs after stations_init() in the boot
     * sequence, so this is the one place that can safely resolve a legacy
     * V0 station index into a current stable ID. Never guessed by casting
     * the index (that was the original bug). */
    if (needs_legacy_resolve) {
        uint32_t resolved_id = CTRL_LAST_STATION_NONE;
        esp_err_t err = stations_resolve_legacy_index(legacy_index, &resolved_id);
        if (err == ESP_OK) {
            s_cfg.last_station_id = resolved_id;
            ESP_LOGI(TAG, "resolved legacy last_station index=%d -> id=%u", legacy_index, resolved_id);
        } else {
            ESP_LOGW(TAG, "could not resolve legacy last_station index=%d (%s); clearing",
                     legacy_index, esp_err_to_name(err));
            s_cfg.last_station_id = CTRL_LAST_STATION_NONE;
        }
        /* Persist the resolved value (and the now-current blob version) so
         * this resolution only ever needs to happen once. */
        esp_err_t save_err = ctrl_cfg_save(&s_cfg);
        if (save_err != ESP_OK) {
            ESP_LOGW(TAG, "failed to persist resolved legacy migration: %s", esp_err_to_name(save_err));
        }
    }
    return ESP_OK;
}

esp_err_t ctrl_start(void)
{
    /* RH-S3-10: require ctrl_init() to have been called first. */
    if (!s_mtx) return ESP_ERR_INVALID_STATE;

    /* 9.2: prevent duplicate orchestrator tasks — never overwrite a live
     * task handle. The task itself reads/clears s_task under this same
     * mutex (see orchestrator_task()'s exit points). */
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_task != NULL) {
        xSemaphoreGive(s_mtx);
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreGive(s_mtx);

    /* orchestrator_task() takes its own ctrl_get_cfg() snapshot at start
     * (9.1) — no config needs to be threaded through xTaskCreate(). */
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
    if (!s_mtx || !s_update_mtx) return ESP_ERR_INVALID_STATE;  /* RH-S3-10: defensive check */
    if (mac && mac[0] && !ctrl_cfg_mac_valid(mac)) return ESP_ERR_INVALID_ARG;

    /* 9.3: dedicated update mutex serializes the whole snapshot -> persist ->
     * publish transaction, so two concurrent setters can't interleave and
     * lose one's update. Persist BEFORE publish — never assign s_cfg before
     * ctrl_cfg_save() succeeds, so a persistence failure leaves s_cfg
     * exactly as it was. */
    xSemaphoreTake(s_update_mtx, portMAX_DELAY);

    ctrl_cfg_t candidate;
    ctrl_get_cfg(&candidate);
    if (mac) strlcpy(candidate.sink_mac, mac, sizeof(candidate.sink_mac));
    candidate.autostart = autostart ? 1 : 0;
    if (volume >= 0) {
        candidate.volume = (volume > 100) ? 100 : (uint8_t)volume;
    }

    esp_err_t e = ctrl_cfg_save(&candidate);
    if (e == ESP_OK) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_cfg = candidate;
        xSemaphoreGive(s_mtx);
    }

    xSemaphoreGive(s_update_mtx);

    ESP_LOGI(TAG, "config set: sink=%s autostart=%u volume=%u (%s)",
             candidate.sink_mac, candidate.autostart, candidate.volume, esp_err_to_name(e));
    return e;
}

esp_err_t ctrl_note_station(uint32_t station_id)
{
    if (!s_mtx || !s_update_mtx) return ESP_ERR_INVALID_STATE;  /* RH-S3-10: defensive check */

    xSemaphoreTake(s_update_mtx, portMAX_DELAY);

    ctrl_cfg_t candidate;
    ctrl_get_cfg(&candidate);
    esp_err_t err = ESP_OK;
    if (station_id != candidate.last_station_id) {
        candidate.last_station_id = station_id;
        err = ctrl_cfg_save(&candidate);
        if (err == ESP_OK) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            s_cfg = candidate;
            xSemaphoreGive(s_mtx);
        }
    }

    xSemaphoreGive(s_update_mtx);
    return err;
}