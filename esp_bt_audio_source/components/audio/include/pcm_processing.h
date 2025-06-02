#ifndef PCM_PROCESSING_H
#define PCM_PROCESSING_H

#include "esp_err.h"
#include <stdint.h>
#include <stdlib.h>

/**
 * Process 16-bit PCM data
 *
 * @param buffer 16-bit PCM buffer
 * @param len Buffer length in samples
 * @return ESP_OK on success
 */
esp_err_t pcm_process_16bit(int16_t* buffer, size_t len);

/**
 * Process 24-bit PCM data
 *
 * @param buffer 24-bit PCM buffer (stored in 32-bit ints)
 * @param len Buffer length in samples
 * @return ESP_OK on success
 */
esp_err_t pcm_process_24bit(int32_t* buffer, size_t len);

/**
 * Convert PCM data to big endian
 *
 * @param buffer PCM buffer
 * @param len Buffer length in samples
 * @return ESP_OK on success
 */
esp_err_t pcm_convert_to_big_endian(int16_t* buffer, size_t len);

/**
 * Convert PCM data to little endian
 *
 * @param buffer PCM buffer
 * @param len Buffer length in samples
 * @return ESP_OK on success
 */
esp_err_t pcm_convert_to_little_endian(int16_t* buffer, size_t len);

/**
 * Convert 16-bit to 24-bit PCM
 *
 * @param src Source 16-bit buffer
 * @param dst Destination 24-bit buffer (32-bit ints)
 * @param len Buffer length in samples
 * @return ESP_OK on success
 */
esp_err_t pcm_convert_16bit_to_24bit(int16_t* src, int32_t* dst, size_t len);

/**
 * Convert 24-bit to 16-bit PCM
 *
 * @param src Source 24-bit buffer (32-bit ints)
 * @param dst Destination 16-bit buffer
 * @param len Buffer length in samples
 * @return ESP_OK on success
 */
esp_err_t pcm_convert_24bit_to_16bit(int32_t* src, int16_t* dst, size_t len);

#endif // PCM_PROCESSING_H
