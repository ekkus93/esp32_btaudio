#include "pcm_processing.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "PCM_PROC";

esp_err_t pcm_process_16bit(int16_t* buffer, size_t len) {
    if (!buffer) return ESP_ERR_INVALID_ARG;
    ESP_LOGI(TAG, "Processing 16-bit PCM data, %d samples", len);
    return ESP_OK;
}

esp_err_t pcm_process_24bit(int32_t* buffer, size_t len) {
    if (!buffer) return ESP_ERR_INVALID_ARG;
    ESP_LOGI(TAG, "Processing 24-bit PCM data, %d samples", len);
    return ESP_OK;
}

esp_err_t pcm_convert_to_big_endian(int16_t* buffer, size_t len) {
    if (!buffer) return ESP_ERR_INVALID_ARG;
    
    for (size_t i = 0; i < len; i++) {
        int16_t val = buffer[i];
        buffer[i] = (val >> 8) | (val << 8); // Swap bytes
    }
    
    return ESP_OK;
}

esp_err_t pcm_convert_to_little_endian(int16_t* buffer, size_t len) {
    if (!buffer) return ESP_ERR_INVALID_ARG;
    
    for (size_t i = 0; i < len; i++) {
        int16_t val = buffer[i];
        buffer[i] = (val >> 8) | (val << 8); // Swap bytes
    }
    
    return ESP_OK;
}

esp_err_t pcm_convert_16bit_to_24bit(int16_t* src, int32_t* dst, size_t len) {
    if (!src || !dst) return ESP_ERR_INVALID_ARG;
    
    for (size_t i = 0; i < len; i++) {
        // Shift left by 8 bits (16-bit to 24-bit)
        dst[i] = ((int32_t)src[i]) << 8;
    }
    
    return ESP_OK;
}

esp_err_t pcm_convert_24bit_to_16bit(int32_t* src, int16_t* dst, size_t len) {
    if (!src || !dst) return ESP_ERR_INVALID_ARG;
    
    for (size_t i = 0; i < len; i++) {
        // Shift right by 8 bits and truncate (24-bit to 16-bit)
        dst[i] = (int16_t)(src[i] >> 8);
    }
    
    return ESP_OK;
}
