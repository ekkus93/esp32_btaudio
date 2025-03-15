#ifndef PING_UTILS_H
#define PING_UTILS_H

#include "esp_err.h"

esp_err_t ping_host(const char *host, int count);

#endif // PING_H
