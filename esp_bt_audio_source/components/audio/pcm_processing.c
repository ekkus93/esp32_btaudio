#include "pcm_processing.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "PCM_PROC";

esp_err_t pcm_process_16bit(int16_t* buffer, size_t len) {
    if (!buffer) return ESP_ERR_INVALID_ARG;
    ESP_LOGI(TAG, "Processing 16-bit PCM data, %zu samples", len);
    return ESP_OK;
}

esp_err_t pcm_process_24bit(int32_t* buffer, size_t len) {
    if (!buffer) return ESP_ERR_INVALID_ARG;
    ESP_LOGI(TAG, "Processing 24-bit PCM data, %zu samples", len);
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

// Convert between big-endian and little-endian formats
int16_t pcm_convert_endianness(int16_t sample) {
    // Swap the bytes of the 16-bit sample
    return ((sample & 0xFF) << 8) | ((sample >> 8) & 0xFF);
}

// Convert stereo PCM to mono by averaging channels - renamed for consistency
void convert_stereo_to_mono(int16_t *stereo_buffer, int16_t *mono_buffer, int frame_count) {
    for (int i = 0; i < frame_count; i++) {
        // Get left and right channel samples
        int32_t left = stereo_buffer[i * 2];
        int32_t right = stereo_buffer[i * 2 + 1];
        
        // Average the channels and store in mono buffer
        // Using 32-bit integers to avoid overflow before division
        mono_buffer[i] = (int16_t)((left + right) / 2);
    }
}

// Add mono to stereo conversion
void convert_mono_to_stereo(int16_t *mono_buffer, int16_t *stereo_buffer, int frame_count) {
    for (int i = 0; i < frame_count; i++) {
        // Duplicate mono sample to both stereo channels
        stereo_buffer[i * 2] = mono_buffer[i];
        stereo_buffer[i * 2 + 1] = mono_buffer[i];
    }
}
