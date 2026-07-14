/* Stub stations.h for host tests */
#ifndef STUB_STATIONS_H
#define STUB_STATIONS_H

#include "esp_err.h"
#include <stdbool.h>

#define RADIO_URL_MAX 256

esp_err_t stations_init(void);
bool stations_get_url(int idx, char *url, size_t url_sz);

#endif
