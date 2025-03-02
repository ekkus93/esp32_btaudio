#ifndef BT_APP_CONN_H
#define BT_APP_CONN_H

#include "esp_gap_bt_api.h"
#include "esp_err.h"
#include <stdbool.h>  

void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
esp_err_t bluetooth_start_discovery(void);
esp_err_t bluetooth_pair_device(const char *mac_str, bool require_pin);

esp_err_t bluetooth_init(void);

// Function to disconnect from a paired device
esp_err_t bluetooth_disconnect_device(void);

// Function to unpair a device
esp_err_t bluetooth_unpair_device(void);

// Function to connect to a paired device
esp_err_t bluetooth_connect_device(const char *mac_str);

// Function to restart the Bluetooth stack
esp_err_t restart_bluetooth_stack(void);

esp_err_t bluetooth_set_device_name(const char *name);
esp_err_t bluetooth_get_device_name(char *name, size_t max_len);

#endif // BT_APP_CONN_H
