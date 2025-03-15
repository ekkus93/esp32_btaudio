#ifndef BT_APP_CONN_H
#define BT_APP_CONN_H

#include "esp_gap_bt_api.h"
#include "esp_err.h"
#include <stdbool.h>  

// Define missing constants if not defined
#ifndef ESP_BT_AUTH_REQ_BONDING
#define ESP_BT_AUTH_REQ_BONDING 0x01
#endif

#ifndef ESP_BT_AUTH_REQ_MITM
#define ESP_BT_AUTH_REQ_MITM 0x04
#endif

#ifndef ESP_BT_SP_AUTHENTICATION_REQUIREMENTS
#define ESP_BT_SP_AUTHENTICATION_REQUIREMENTS 0x0D
#endif

esp_err_t bluetooth_pair_device(const char *mac_str, bool require_pin);

// Function to disconnect from a paired device
esp_err_t bluetooth_disconnect_device(void);

// Function to unpair a device
esp_err_t bluetooth_unpair_device(void);

// Function to connect to a paired device
esp_err_t bluetooth_connect_device(const char *mac_str);

#endif // BT_APP_CONN_H
