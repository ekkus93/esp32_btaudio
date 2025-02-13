#ifndef PING_H
#define PING_H

#include "esp_err.h"

// Ping a host
esp_err_t ping_host(const char *host, int count);

#endif // PING_H
