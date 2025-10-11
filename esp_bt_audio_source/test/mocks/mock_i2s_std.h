// Moved from top-level test/mocks/mock_i2s_std.h
#ifndef MOCK_I2S_STD_H
#define MOCK_I2S_STD_H

#include "esp_err.h"

static inline esp_err_t i2s_new_channel_ExpectAnyArgsAndReturn(esp_err_t r) { (void)r; return ESP_OK; }
static inline esp_err_t i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(esp_err_t r) { (void)r; return ESP_OK; }

#endif // MOCK_I2S_STD_H
