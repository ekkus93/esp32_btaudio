/*
 * stations — device wrapper around the pure station_store (RADIO-1c): NVS
 * persistence, first-boot seeding, and a mutex. Mutations persist immediately.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load presets from NVS, seeding defaults on first boot. Call once after NVS. */
esp_err_t stations_init(void);

int  stations_count(void);
/* Copy entry idx into name/url. Returns false on a bad index. */
bool stations_get(int idx, char *name, size_t nsz, char *url, size_t usz);
/* Copy just the URL of entry idx (for play-by-id). */
bool stations_get_url(int idx, char *url, size_t usz);

/* Mutations (persist to NVS). add returns the new index or -1. */
int  stations_add(const char *name, const char *url);
bool stations_update(int idx, const char *name, const char *url);
bool stations_remove(int idx);

#ifdef __cplusplus
}
#endif
