#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_bt.h"          // Include main Bluetooth definitions
#include "esp_bt_main.h"     // Include Bluetooth main functions
#include "esp_bt_device.h"   // Include Bluetooth device functions
#include "esp_gap_bt_api.h"  // Include GAP event definitions
#include "nvs_flash.h"       // Add NVS Flash header
#include "esp_bt_defs.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"    // Add AVRCP API include

// Initialize Bluetooth stack
esp_err_t bluetooth_init(void);

// Initialize A2DP
esp_err_t init_a2dp(void);

// Start Bluetooth discovery
esp_err_t bluetooth_start_discovery(void);

// Pair with a Bluetooth device
esp_err_t bluetooth_pair_device(const char *mac_str, bool require_pin);

// Function to connect to a paired device
esp_err_t bluetooth_connect_device(const char *mac_str);

// Function to disconnect from a paired device
esp_err_t bluetooth_disconnect_device(void);

// Function to unpair a device
esp_err_t bluetooth_unpair_device(void);

// A2DP Sink callback for audio data
void audio_data_cb(uint8_t *data, uint32_t len);

// GAP event handler
void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

// A2DP callback function
void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);

// AVRCP controller callback
void avrc_ct_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);

#endif // BLUETOOTH_H
