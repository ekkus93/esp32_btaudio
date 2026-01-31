#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

// Initialize NVS storage for the app
esp_err_t nvs_storage_init(void);

// Volume
esp_err_t nvs_storage_get_volume(uint8_t* volume);
esp_err_t nvs_storage_set_volume(uint8_t volume);

// I2S pins: bclk, ws, din, dout
esp_err_t nvs_storage_get_i2s_pins(int* bclk, int* ws, int* din, int* dout);
esp_err_t nvs_storage_set_i2s_pins(int bclk, int ws, int din, int dout);

// Device name
esp_err_t nvs_storage_get_device_name(char* buf, size_t buf_len);
esp_err_t nvs_storage_set_device_name(const char* name);

// Default PIN
esp_err_t nvs_storage_get_default_pin(char* buf, size_t buf_len);
esp_err_t nvs_storage_set_default_pin(const char* pin);

// Audio autostart configuration
// Returns ESP_OK if value is set, ESP_ERR_NOT_FOUND if not set (defaults to true)
// autostart: receives 1 for enabled, 0 for disabled
esp_err_t nvs_storage_get_audio_autostart(uint8_t* autostart);
esp_err_t nvs_storage_set_audio_autostart(uint8_t autostart);

// Paired devices persistence (simple indexed list stored in NVS)
// count: number of stored paired devices
esp_err_t nvs_storage_get_paired_count(int* count);
// Fetch paired device fields by index (0-based). Returns ESP_OK if present.
esp_err_t nvs_storage_get_paired_device_by_index(int index, char* mac, size_t mac_len, char* name, size_t name_len);
// Add/remove/clear paired devices
esp_err_t nvs_storage_add_paired_device(const char* mac, const char* name);
esp_err_t nvs_storage_remove_paired_device(const char* mac);
esp_err_t nvs_storage_clear_paired_devices(void);
