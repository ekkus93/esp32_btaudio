#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/i2s_std.h"

typedef struct {
    uint32_t sample_rate_hz;
    i2s_data_bit_width_t bit_width;
    i2s_slot_mode_t slot_mode;
} audio_i2s_config_t;

#define AUDIO_I2S_DEFAULT_CONFIG() \
    (audio_i2s_config_t){ .sample_rate_hz = 44100, .bit_width = I2S_DATA_BIT_WIDTH_16BIT, .slot_mode = I2S_SLOT_MODE_STEREO }

esp_err_t audio_i2s_init(const audio_i2s_config_t *config);
esp_err_t audio_i2s_start(void);
esp_err_t audio_i2s_stop(void);
esp_err_t audio_i2s_deinit(void);
esp_err_t audio_i2s_read(uint8_t *dest, size_t len, size_t *bytes_read, uint32_t timeout_ms);
