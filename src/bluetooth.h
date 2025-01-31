#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_bt.h"          // Include main Bluetooth definitions
#include "esp_bt_main.h"     // Include Bluetooth main functions
#include "esp_bt_device.h"   // Include Bluetooth device functions
#include "esp_gap_bt_api.h"      // Include GAP event definitions

// Initialize Bluetooth functionality
esp_err_t bluetooth_init(void);

// Start Bluetooth discovery
esp_err_t bluetooth_start_discovery(void);

// Pair with a Bluetooth device
esp_err_t bluetooth_pair_device(const char *mac_str, bool require_pin);

// Get mutex handle
SemaphoreHandle_t bluetooth_get_mutex(void);

// A2DP Sink callback for audio data
void audio_data_cb(uint8_t *data, uint32_t len);

// Add this declaration
void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

#endif // BLUETOOTH_H
