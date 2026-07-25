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
 * stations_add() passes the new index out via *out_idx (may be NULL).
 * Both pass the specific store result out via *reason (may be NULL) so the
 * caller can report exactly why a mutation was rejected (full vs duplicate
 * vs invalid URL vs too long) instead of a collapsed catch-all. On a
 * persist-layer failure *reason is left STATION_OK and the esp_err_t return
 * carries the error. */
esp_err_t stations_add(const char *name, const char *url, int *out_idx,
                       station_result_t *reason);
/* update/remove/move identify the target by its STABLE station id (the value
 * exposed as "id" by GET /api/stations and sent back by the web UI), NOT the
 * array index — resolution to an index happens internally under the store
 * lock. An unknown id yields STATION_ERR_NOT_FOUND (ESP_ERR_INVALID_ARG). */
esp_err_t stations_update(uint32_t id, const char *name, const char *url,
                          station_result_t *reason);
esp_err_t stations_remove(uint32_t id);

/* Reorder: swap the entry with stable id `id` with its neighbour
 * (delta -1 up, +1 down). */
esp_err_t stations_move(uint32_t id, int delta);

/* Resolve a legacy ctrl "last station index" (from the pre-ID V0 control
 * blob) to its current stable ID (FIX3 §9.4). legacy_index < 0 always
 * resolves to *out_station_id = STATION_ID_NONE, ESP_OK. Returns
 * ESP_ERR_NOT_FOUND if stations were freshly seeded this boot (no real
 * legacy list exists to resolve against) and ESP_ERR_INVALID_ARG if
 * legacy_index is out of range. Call only after stations_init() succeeds. */
esp_err_t stations_resolve_legacy_index(int16_t legacy_index, uint32_t *out_station_id);

/* Recovery-only (FIX3 §8.3 deliberately never auto-runs this): erase both
 * the V2 and legacy NVS station blobs, then re-seed and re-initialize in
 * place — no reboot required. For a CRC-corrupt blob that stations_init()
 * refused to touch. Local physical-presence console path only (never
 * reachable over HTTP), same trust boundary as AUTH ROTATE. Returns the
 * result of the re-seed/persist; on success capabilities.stations becomes
 * true immediately. */
esp_err_t stations_reset_persisted(void);

#ifdef __cplusplus
}
#endif
