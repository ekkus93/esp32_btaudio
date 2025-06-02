#include "i2s_audio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "I2S_AUDIO";
static bool driver_installed = false;
static i2s_config_t current_config;

esp_err_t i2s_driver_init(int sample_rate, i2s_bits_per_sample_t bits_per_sample, i2s_channel_fmt_t channel_fmt) {
    ESP_LOGI(TAG, "Initializing I2S driver: %d Hz, %d bits, channel format %d", 
             sample_rate, bits_per_sample, channel_fmt);
    
    // Initialize config
    memset(&current_config, 0, sizeof(current_config));
    current_config.mode = I2S_MODE_MASTER | I2S_MODE_TX;
    current_config.sample_rate = sample_rate;
    current_config.bits_per_sample = bits_per_sample;
    current_config.channel_format = channel_fmt;
    current_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    current_config.dma_buf_count = 8;
    current_config.dma_buf_len = 64;
    
    driver_installed = true;
    return ESP_OK;
}

bool i2s_is_driver_installed(void) {
    return driver_installed;
}

esp_err_t i2s_get_config(i2s_config_t* config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    if (!driver_installed) return ESP_ERR_INVALID_STATE;
    
    memcpy(config, &current_config, sizeof(i2s_config_t));
    return ESP_OK;
}

esp_err_t i2s_configure_standard_mode(void) {
    if (!driver_installed) {
        return i2s_driver_init(44100, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_FMT_RIGHT_LEFT);
    }
    
    current_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    current_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    current_config.sample_rate = 44100;
    
    return ESP_OK;
}

esp_err_t i2s_write_samples(int16_t* buffer, size_t len, size_t* bytes_written) {
    if (!driver_installed) return ESP_ERR_INVALID_STATE;
    if (!buffer) return ESP_ERR_INVALID_ARG;
    
    if (bytes_written) {
        *bytes_written = len * sizeof(int16_t);
    }
    
    return ESP_OK;
}

// Implementation of remaining functions...
esp_err_t i2s_config_mono(int sample_rate, i2s_bits_per_sample_t bits_per_sample) {
    if (!driver_installed) {
        return i2s_driver_init(sample_rate, bits_per_sample, I2S_CHANNEL_FMT_ONLY_LEFT);
    }
    
    current_config.sample_rate = sample_rate;
    current_config.bits_per_sample = bits_per_sample;
    current_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    
    return ESP_OK;
}

esp_err_t i2s_config_stereo(int sample_rate, i2s_bits_per_sample_t bits_per_sample) {
    if (!driver_installed) {
        return i2s_driver_init(sample_rate, bits_per_sample, I2S_CHANNEL_FMT_RIGHT_LEFT);
    }
    
    current_config.sample_rate = sample_rate;
    current_config.bits_per_sample = bits_per_sample;
    current_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    
    return ESP_OK;
}

i2s_channel_fmt_t i2s_get_channel_format(void) {
    return current_config.channel_format;
}

esp_err_t i2s_write_mono_samples(int16_t* buffer, size_t len) {
    if (!driver_installed) return ESP_ERR_INVALID_STATE;
    if (!buffer) return ESP_ERR_INVALID_ARG;
    
    return ESP_OK;
}

esp_err_t i2s_write_stereo_samples(int16_t* buffer, size_t len) {
    if (!driver_installed) return ESP_ERR_INVALID_STATE;
    if (!buffer) return ESP_ERR_INVALID_ARG;
    
    return ESP_OK;
}

esp_err_t i2s_process_channels(int16_t* buffer, size_t len) {
    if (!buffer) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

esp_err_t i2s_convert_stereo_to_mono(int16_t* stereo_buffer, int16_t* mono_buffer, size_t frames) {
    if (!stereo_buffer || !mono_buffer) return ESP_ERR_INVALID_ARG;
    
    for (size_t i = 0; i < frames; i++) {
        // Average left and right channels
        mono_buffer[i] = (int16_t)(((int32_t)stereo_buffer[i*2] + (int32_t)stereo_buffer[i*2+1]) / 2);
    }
    
    return ESP_OK;
}

esp_err_t i2s_convert_mono_to_stereo(int16_t* mono_buffer, int16_t* stereo_buffer, size_t mono_samples) {
    if (!mono_buffer || !stereo_buffer) return ESP_ERR_INVALID_ARG;
    
    for (size_t i = 0; i < mono_samples; i++) {
        // Duplicate each sample to left and right channels
        stereo_buffer[i*2] = mono_buffer[i];     // Left
        stereo_buffer[i*2+1] = mono_buffer[i];   // Right
    }
    
    return ESP_OK;
}
