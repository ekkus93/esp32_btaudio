/*
 * stations — device wrapper around the pure station_store (RADIO-1c): NVS
 * persistence (STN2 versioned blob with CRC-32), first-boot seeding, and a
 * mutex. Mutations persist immediately. Stable IDs survive reorder.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "station_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load presets from NVS, seeding defaults on first boot. Call once after NVS.
 * Idempotent — safe to call multiple times. Returns ESP_OK if already loaded. */
esp_err_t stations_init(void);

int  stations_count(void);

/* Copy entry idx into name/url. Returns false on a bad index.
 * out_id is optional — if non-NULL, the station's stable ID is written. */
bool stations_get(int idx, char *name, size_t nsz,
                  char *url, size_t usz, uint32_t *out_id);

/* Copy just the URL of entry idx. */
bool stations_get_url(int idx, char *url, size_t usz);

/* Mutations (persist to NVS). Returns ESP_OK on success.
 * stations_add() passes the new index out via *out_idx (may be NULL). */
esp_err_t stations_add(const char *name, const char *url, int *out_idx);
esp_err_t stations_update(int idx, const char *name, const char *url);
esp_err_t stations_remove(int idx);

/* Reorder: swap entry idx with its neighbour (delta -1 up, +1 down). */
esp_err_t stations_move(int idx, int delta);

#ifdef __cplusplus
}
#endif
