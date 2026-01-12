#include "i2s_audio.h"

static bool s_installed = false;
static uint32_t s_sample_rate = 0;
static i2s_data_bit_width_t s_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
static i2s_slot_mode_t s_slot_mode = I2S_SLOT_MODE_STEREO;

static bool bit_width_supported(i2s_data_bit_width_t bit_width)
{
    switch (bit_width) {
#ifdef I2S_DATA_BIT_WIDTH_8BIT
        case I2S_DATA_BIT_WIDTH_8BIT:
#endif
        case I2S_DATA_BIT_WIDTH_16BIT:
        case I2S_DATA_BIT_WIDTH_24BIT:
        case I2S_DATA_BIT_WIDTH_32BIT:
            return true;
        default:
            return false;
    }
}

static void set_config(uint32_t sample_rate, i2s_data_bit_width_t bit_width, i2s_slot_mode_t slot_mode)
{
    s_sample_rate = sample_rate;
    s_bit_width = bit_width;
    s_slot_mode = slot_mode;
    s_installed = true;
}

esp_err_t i2s_driver_init(uint32_t sample_rate, i2s_data_bit_width_t bit_width, i2s_slot_mode_t slot_mode)
{
    if (!bit_width_supported(bit_width)) {
        return ESP_ERR_INVALID_ARG;
    }
    set_config(sample_rate, bit_width, slot_mode);
    return ESP_OK;
}

esp_err_t i2s_config_mono(uint32_t sample_rate, i2s_data_bit_width_t bit_width)
{
    return i2s_driver_init(sample_rate, bit_width, I2S_SLOT_MODE_MONO);
}

esp_err_t i2s_config_stereo(uint32_t sample_rate, i2s_data_bit_width_t bit_width)
{
    return i2s_driver_init(sample_rate, bit_width, I2S_SLOT_MODE_STEREO);
}

esp_err_t i2s_configure_standard_mode(void)
{
    return i2s_driver_init(44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
}

esp_err_t i2s_driver_deinit(void)
{
    s_installed = false;
    s_sample_rate = 0;
    s_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
    s_slot_mode = I2S_SLOT_MODE_STEREO;
    return ESP_OK;
}

bool i2s_is_driver_installed(void)
{
    return s_installed;
}

i2s_slot_mode_t i2s_get_channel_format(void)
{
    return s_slot_mode;
}

esp_err_t i2s_write_samples(const int16_t *samples, size_t count, size_t *bytes_written)
{
    if (!s_installed) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!samples) {
        return ESP_ERR_INVALID_ARG;
    }
    if (bytes_written) {
        *bytes_written = count * sizeof(int16_t);
    }
    (void)s_sample_rate;
    (void)s_bit_width;
    return ESP_OK;
}

esp_err_t i2s_write_mono_samples(const int16_t *samples, size_t count)
{
    if (!s_installed) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!samples) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)count;
    return ESP_OK;
}

esp_err_t i2s_write_stereo_samples(const int16_t *samples, size_t count)
{
    if (!s_installed) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!samples) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)count;
    return ESP_OK;
}

esp_err_t i2s_convert_mono_to_stereo(const int16_t *mono, int16_t *stereo, size_t mono_samples)
{
    if (!mono || !stereo) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < mono_samples; ++i) {
        stereo[2 * i] = mono[i];
        stereo[2 * i + 1] = mono[i];
    }
    return ESP_OK;
}

esp_err_t i2s_convert_stereo_to_mono(const int16_t *stereo, int16_t *mono, size_t stereo_samples)
{
    if (!stereo || !mono) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < stereo_samples; ++i) {
        const int32_t left = stereo[2 * i];
        const int32_t right = stereo[2 * i + 1];
        mono[i] = (int16_t)((left + right) / 2);
    }
    return ESP_OK;
}

esp_err_t i2s_process_channels(int16_t *samples, size_t sample_count)
{
    if (!samples) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)sample_count;
    return ESP_OK;
}
