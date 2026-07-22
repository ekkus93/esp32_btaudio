/* Stub stations.h for host tests */
#ifndef STUB_STATIONS_H
#define STUB_STATIONS_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define RADIO_URL_MAX 256

esp_err_t stations_init(void);
int stations_count(void);
bool stations_get(int idx, char *name, size_t nsz,
                  char *url, size_t usz, uint32_t *out_id);
bool stations_get_url(int idx, char *url, size_t url_sz);
esp_err_t stations_resolve_legacy_index(int16_t legacy_index, uint32_t *out_station_id);

#endif
