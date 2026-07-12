/*
 * ctrl — boot orchestrator device glue (CTRL-1b).
 *
 * Loads ctrl_cfg, and if autostart is enabled with a valid sink MAC, runs the
 * ctrl_sm orchestrator on its own task: waits for WiFi, CONNECTs the sink over
 * bt_link, STARTs A2DP, resumes the last station, then health-polls STATUS and
 * reconnects on drop. Also exposes config accessors for the web API and a hook
 * to record the last played station.
 */
#pragma once

#include "esp_err.h"
#include "ctrl_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load config and spawn the orchestrator task. Call after bt_link, wifi_mgr,
 * radio and stations are initialised. No-op-ish (task exits) if autostart is
 * off or the sink MAC is unset/invalid. */
esp_err_t ctrl_start(void);

/* Snapshot the current config (thread-safe). */
void ctrl_get_cfg(ctrl_cfg_t *out);

/* Update the target sink + autostart flag and persist. A non-empty mac must be
 * a valid "XX:.." MAC (else ESP_ERR_INVALID_ARG). */
esp_err_t ctrl_set_sink(const char *mac, bool autostart);

/* Record the last played station index (persisted) so autostart can resume it.
 * Called from the radio play path; cheap no-op if unchanged. */
void ctrl_note_station(int idx);

#ifdef __cplusplus
}
#endif
