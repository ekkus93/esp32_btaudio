#ifndef BT_APP_DISCOVERY_H
#define BT_APP_DISCOVERY_H

#include <esp_err.h>

esp_err_t bluetooth_start_discovery(void);
esp_err_t bluetooth_safe_start_discovery(void);

#endif // BT_APP_DISCOVERY_H