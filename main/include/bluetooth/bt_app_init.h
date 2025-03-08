#ifndef BT_APP_INIT_H
#define BT_APP_INIT_H

#include "esp_err.h"

/**
 * @brief Initialize the Bluetooth stack
 * 
 * @return ESP_OK on success, ESP_FAIL or other error code on failure
 */
esp_err_t bluetooth_init(void);

#endif /* BT_APP_INIT_H */
