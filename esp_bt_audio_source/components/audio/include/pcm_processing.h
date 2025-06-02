#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Process 16-bit PCM audio data
 * 
 * @param buffer Pointer to PCM data
 * @param len Number of samples
 * @return ESP_OK if successful, error otherwise
 */
esp_err_t pcm_process_16bit(int16_t* buffer, size_t len);

/**
 * @brief Process 24-bit PCM audio data
 * 
 * @param buffer Pointer to PCM data
 * @param len Number of samples
 * @return ESP_OK if successful, error otherwise
 */
esp_err_t pcm_process_24bit(int32_t* buffer, size_t len);

/**
 * @brief Convert PCM data to big-endian format
 * 
 * @param buffer Pointer to PCM data
 * @param len Number of samples
 * @return ESP_OK if successful, error otherwise
 */
esp_err_t pcm_convert_to_big_endian(int16_t* buffer, size_t len);

/**
 * @brief Convert PCM data to little-endian format
 * 
 * @param buffer Pointer to PCM data
 * @param len Number of samples
 * @return ESP_OK if successful, error otherwise
 */
esp_err_t pcm_convert_to_little_endian(int16_t* buffer, size_t len);

/**
 * @brief Convert 16-bit PCM data to 24-bit
 * 
 * @param src Source 16-bit PCM data
 * @param dst Destination 24-bit PCM data
 * @param len Number of samples
 * @return ESP_OK if successful, error otherwise
 */
esp_err_t pcm_convert_16bit_to_24bit(int16_t* src, int32_t* dst, size_t len);

/**
 * @brief Convert 24-bit PCM data to 16-bit
 * 
 * @param src Source 24-bit PCM data
 * @param dst Destination 16-bit PCM data
 * @param len Number of samples
 * @return ESP_OK if successful, error otherwise
 */
esp_err_t pcm_convert_24bit_to_16bit(int32_t* src, int16_t* dst, size_t len);

/**
 * @brief Swap endianness of a 16-bit sample
 * 
 * @param sample 16-bit sample
 * @return Endianness-swapped sample
 */
int16_t pcm_convert_endianness(int16_t sample);

/**
 * @brief Convert stereo PCM data to mono by averaging channels
 *
 * @param stereo_buffer Interleaved stereo PCM data (L R L R ...)
 * @param mono_buffer Output buffer for mono PCM data
 * @param frame_count Number of stereo frames (pairs of L/R samples)
 */
void convert_stereo_to_mono(int16_t *stereo_buffer, int16_t *mono_buffer, int frame_count);

/**
 * @brief Convert mono PCM data to stereo by duplicating samples
 *
 * @param mono_buffer Mono PCM data
 * @param stereo_buffer Output buffer for interleaved stereo PCM data
 * @param frame_count Number of mono samples
 */
void convert_mono_to_stereo(int16_t *mono_buffer, int16_t *stereo_buffer, int frame_count);

#ifdef __cplusplus
}
#endif
