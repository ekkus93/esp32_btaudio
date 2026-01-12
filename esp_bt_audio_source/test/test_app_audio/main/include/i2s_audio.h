#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2s_std.h"

esp_err_t i2s_driver_init(uint32_t sample_rate, i2s_data_bit_width_t bit_width, i2s_slot_mode_t slot_mode);
esp_err_t i2s_config_mono(uint32_t sample_rate, i2s_data_bit_width_t bit_width);
esp_err_t i2s_config_stereo(uint32_t sample_rate, i2s_data_bit_width_t bit_width);
esp_err_t i2s_configure_standard_mode(void);
esp_err_t i2s_driver_deinit(void);
bool i2s_is_driver_installed(void);
i2s_slot_mode_t i2s_get_channel_format(void);
esp_err_t i2s_write_samples(const int16_t *samples, size_t count, size_t *bytes_written);
esp_err_t i2s_write_mono_samples(const int16_t *samples, size_t count);
esp_err_t i2s_write_stereo_samples(const int16_t *samples, size_t count);
esp_err_t i2s_convert_mono_to_stereo(const int16_t *mono, int16_t *stereo, size_t mono_samples);
esp_err_t i2s_convert_stereo_to_mono(const int16_t *stereo, int16_t *mono, size_t stereo_samples);
esp_err_t i2s_process_channels(int16_t *samples, size_t sample_count);
