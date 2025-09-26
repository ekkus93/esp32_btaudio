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
