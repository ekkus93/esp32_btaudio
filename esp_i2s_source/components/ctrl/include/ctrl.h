/*
 * ctrl — boot orchestrator device glue (CTRL-1b).
 *
 * Loads ctrl_cfg, and if autostart is enabled with a valid sink MAC, runs the
 * ctrl_sm orchestrator on its own task: waits for WiFi, CONNECTs the sink over
 * bt_link, STARTs A2DP, resumes the last station, then health-polls STATUS and
 * reconnects on drop. Also exposes config accessors for the web API and a hook
 * to record the last played station.
 *
 * RH-S3-10: ctrl_init() creates the mutex and zeroes state BEFORE the web UI
 * starts (so HTTP handlers never see a NULL mutex). ctrl_start() then spawns
 * the orchestrator task.
 */
#pragma once

#include "esp_err.h"
#include "ctrl_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create the controller mutex and load config. Must be called before
 * web_ui_start() so HTTP handlers never access an uninitialised mutex.
 * Idempotent — safe to call multiple times. Returns ESP_ERR_NO_MEM if mutex
 * creation fails. */
esp_err_t ctrl_init(void);

/* Load config and spawn the orchestrator task. Call after bt_link, wifi_mgr,
 * radio and stations are initialised. No-op-ish (task exits) if autostart is
 * off or the sink MAC is unset/invalid. Requires ctrl_init() to have been
 * called first (returns ESP_ERR_INVALID_STATE otherwise). */
esp_err_t ctrl_start(void);

/* Snapshot the current config (thread-safe). */
void ctrl_get_cfg(ctrl_cfg_t *out);

/* Update the target sink + autostart flag + autostart volume and persist. A
 * non-empty mac must be a valid "XX:.." MAC (else ESP_ERR_INVALID_ARG). Pass
 * volume < 0 to leave the stored volume unchanged; otherwise clamped to [0,100]. */
esp_err_t ctrl_set_sink(const char *mac, bool autostart, int volume);

/* Record the last played station index (persisted) so autostart can resume it.
 * Called from the radio play path; cheap no-op if unchanged. */
void ctrl_note_station(int idx);

/* Run a Bluetooth device scan with A2DP suspended for a clean inquiry: stops
 * the stream + disconnects the sink, runs SCAN (results arrive over the /ws as
 * INFO|SCAN|RESULT), then reconnects the sink and resumes the station. Returns
 * ESP_ERR_INVALID_STATE if a scan is already running. */
esp_err_t ctrl_scan(void);

/* True while a suspend-and-scan cycle is in progress (~20 s). */
bool ctrl_scan_active(void);

#ifdef __cplusplus
}
#endif
