/*
 * ctrl_cfg — NVS-backed orchestration config (CTRL-1a).
 *
 * Persists what the boot orchestrator (CTRL-1b) needs to bring the system to
 * music with no human interaction: the target A2DP sink MAC, an autostart
 * flag, and the last station index to resume. Mirrors the magic-guarded blob
 * pattern in radio/stations.c (namespace "ctrl"). NVS load/save are device
 * only; ctrl_cfg_mac_valid() is pure and host-tested.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CTRL_MAC_LEN      18   /* "AA:BB:CC:DD:EE:FF" + NUL */
#define CTRL_STATION_NONE (-1) /* last_station: idle (no radio resume) */

typedef struct {
    char    sink_mac[CTRL_MAC_LEN]; /* target A2DP sink; "" = none set */
    uint8_t autostart;              /* 0/1: connect + resume on boot */
    int16_t last_station;           /* station index to resume, or CTRL_STATION_NONE */
} ctrl_cfg_t;

/* Validate a Bluetooth MAC string: exactly "XX:XX:XX:XX:XX:XX", hex digits,
 * colon-separated (case-insensitive). Pure — no NVS. */
bool ctrl_cfg_mac_valid(const char *mac);

/* Fill *out from NVS, or with defaults (mac "", autostart 0, last_station
 * NONE) on first boot / invalid blob. Never fails; always yields a usable cfg. */
void ctrl_cfg_load(ctrl_cfg_t *out);

/* Persist *cfg to NVS. Returns ESP_OK on success. */
#ifdef ESP_PLATFORM
#include "esp_err.h"
esp_err_t ctrl_cfg_save(const ctrl_cfg_t *cfg);
#endif

#ifdef __cplusplus
}
#endif
