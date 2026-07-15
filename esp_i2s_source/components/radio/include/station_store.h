/*
 * station_store — pure preset CRUD for internet-radio stations (RADIO-1c).
 * No ESP-IDF deps; host-tested. Add/edit/delete with URL validation,
 * blank-name defaulting (to the URL host), and exact-URL dedupe.
 * Stable IDs survive reorder — the device layer (stations.c) wraps this with
 * NVS persistence, versioned blobs (STN2 magic + CRC-32), and a mutex.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STATION_MAX       40   /* capacity (>= 32 per RADIO-1c-i) */
#define STATION_NAME_MAX  48
#define STATION_URL_MAX  256

#define STATION_ID_NONE   0u   /* sentinel — no station selected */

/* Precise error codes for station operations.
 * Not ESP_ERR codes — these are application-level and map cleanly to HTTP 400. */
typedef enum {
    STATION_OK            = 0,
    STATION_ERR_INVALID_ARG,
    STATION_ERR_INVALID_URL,
    STATION_ERR_TOO_LONG,
    STATION_ERR_DUPLICATE,
    STATION_ERR_FULL,
    STATION_ERR_NOT_FOUND,
    STATION_ERR_PERSIST,
} station_result_t;

typedef struct {
    uint32_t  id;         /* stable identity — never changes after assign */
    char      name[STATION_NAME_MAX];
    char      url[STATION_URL_MAX];
} station_t;

typedef struct {
    uint32_t  next_id;    /* next ID to assign (sequential, starts at 1) */
    int       count;
    station_t items[STATION_MAX];
} station_store_t;

/* Initialise a store (next_id reset to 1, count to 0). */
void station_store_init(station_store_t *s);

/* Validate a URL for station use. Returns STATION_OK if valid, error code
 * otherwise. Checks: non-NULL, non-empty, fits in STATION_URL_MAX,
 * http/https scheme, no control chars, and (for device builds) rejects
 * loopback/link-local/private destinations unless
 * CONFIG_ESP_I2S_SOURCE_ALLOW_LOCAL_STREAMS is set. */
station_result_t station_validate_url(const char *url);

/* Legacy — returns true iff url passes station_validate_url(). */
bool station_url_valid(const char *url);

/* Add a station. Returns STATION_OK on success (new index via *out_idx),
 * or an error code. If name is NULL/empty it defaults to the URL host.
 * Rejects invalid URL, duplicates (exact-URL), and full store. */
station_result_t station_store_add(station_store_t *s,
                                    const char *name, const char *url,
                                    int *out_idx);

/* Replace the entry at idx. Returns STATION_OK on success. Rejects bad index,
 * invalid URL, and collisions with another entry's URL. */
station_result_t station_store_update(station_store_t *s,
                                       int idx, const char *name, const char *url);

/* Remove the entry at idx, shifting the rest down. Returns STATION_OK on success. */
station_result_t station_store_remove(station_store_t *s, int idx);

/* Swap the entry at idx with its neighbour (delta -1 up, +1 down).
 * Returns STATION_OK on success. */
station_result_t station_store_move(station_store_t *s, int idx, int delta);

/* Index of the entry with exactly this URL, or -1. */
int station_store_find(const station_store_t *s, const char *url);

/* Index of the entry with this stable ID, or -1. */
int station_store_index_by_id(const station_store_t *s, uint32_t id);

#ifdef __cplusplus
}
#endif
