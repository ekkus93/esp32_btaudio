/*
 * ctrl_cfg — NVS-backed orchestration config (CTRL-1a).
 *
 * Persists what the boot orchestrator (CTRL-1b) needs to bring the system to
 * music with no human interaction: the target A2DP sink MAC, an autostart
 * flag, and the last station ID to resume. Mirrors the magic-guarded blob
 * pattern in radio/stations.c (namespace "ctrl"). NVS load/save are device
 * only; ctrl_cfg_mac_valid() is pure and host-tested.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CTRL_MAC_LEN      18   /* "AA:BB:CC:DD:EE:FF" + NUL */
#define CTRL_LAST_STATION_NONE  0u /* last_station_id: idle (no radio resume) */

/* Deprecated alias — kept for migration from old index-based format. */
#define CTRL_STATION_NONE (-1)

#define CTRL_VOLUME_DEFAULT 10         /* conservative earbud level (0..100) */

typedef struct {
    char    sink_mac[CTRL_MAC_LEN]; /* target A2DP sink; "" = none set */
    uint8_t autostart;              /* 0/1: connect + resume on boot */
    uint32_t last_station_id;       /* station stable ID to resume, or CTRL_LAST_STATION_NONE */
    uint8_t volume;                 /* WROOM32 VOLUME (0..100) to apply on connect
                                     * — the WROOM32 resets VOL to 40 on a fresh
                                     * A2DP link, so autostart re-asserts this */
} ctrl_cfg_t;

/* Validate a Bluetooth MAC string: exactly "XX:XX:XX:XX:XX:XX", hex digits,
 * colon-separated (case-insensitive). Pure — no NVS. */
bool ctrl_cfg_mac_valid(const char *mac);

/* Fill *out from NVS, or with defaults (mac "", autostart 0, last_station_id
 * 0) on first boot / invalid blob. Never fails; always yields a usable cfg.
 *
 * FIX3 9.4 (coordinator design): if the persisted blob is the old V0
 * (index-based) format, this does NOT guess a stable station ID by casting
 * the index. Instead *out->last_station_id is left at CTRL_LAST_STATION_NONE,
 * *out_needs_legacy_resolve is set true, and *out_legacy_index carries the
 * raw V0 index (CTRL_STATION_NONE if the old blob itself had none). The
 * caller — ctrl_init(), which runs after stations_init() in the boot
 * sequence — is the coordinator: it must resolve the index via
 * stations_resolve_legacy_index() and persist the result before anything
 * else reads last_station_id. out_needs_legacy_resolve/out_legacy_index
 * must both be non-NULL. */
void ctrl_cfg_load(ctrl_cfg_t *out, bool *out_needs_legacy_resolve, int16_t *out_legacy_index);

/* Persist *cfg to NVS. Returns ESP_OK on success.
 * Definition is ESP_PLATFORM-only (see ctrl_cfg.c); host tests link a stub
 * (mocks/stubs/ctrl_cfg_host.c). The declaration stays unconditional so
 * host-side callers get a real prototype instead of an implicit one. */
esp_err_t ctrl_cfg_save(const ctrl_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
