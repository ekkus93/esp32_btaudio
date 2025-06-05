#ifndef I2S_AUDIO_H
#define I2S_AUDIO_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/i2s_types_legacy.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize I2S driver
 *
 * @param sample_rate Sample rate in Hz
 * @param bits_per_sample Bits per sample
 * @param channel_fmt Channel format (mono/stereo)
 * @return ESP_OK on success
 */
esp_err_t i2s_driver_init(int sample_rate, i2s_bits_per_sample_t bits_per_sample, i2s_channel_fmt_t channel_fmt);

/**
 * Check if I2S driver is installed
 *
 * @return true if installed
 */
bool i2s_is_driver_installed(void);

/**
 * Get current I2S configuration
 *
 * @param config Pointer to store configuration
 * @return ESP_OK on success
 */
esp_err_t i2s_get_config(i2s_config_t* config);

/**
 * Configure I2S for standard mode
 *
 * @return ESP_OK on success
 */
esp_err_t i2s_configure_standard_mode(void);

/**
 * Write samples to I2S
 *
 * @param buffer Buffer containing samples
 * @param len Length of buffer in items
 * @param bytes_written Number of bytes written
 * @return ESP_OK on success
 */
esp_err_t i2s_write_samples(int16_t* buffer, size_t len, size_t* bytes_written);

/**
 * Configure I2S for mono operation
 *
 * @param sample_rate Sample rate in Hz
 * @param bits_per_sample Bits per sample
 * @return ESP_OK on success
 */
esp_err_t i2s_config_mono(int sample_rate, i2s_bits_per_sample_t bits_per_sample);

/**
 * Configure I2S for stereo operation
 *
 * @param sample_rate Sample rate in Hz
 * @param bits_per_sample Bits per sample
 * @return ESP_OK on success
 */
esp_err_t i2s_config_stereo(int sample_rate, i2s_bits_per_sample_t bits_per_sample);

/**
 * Get current channel format
 *
 * @return Current channel format
 */
i2s_channel_fmt_t i2s_get_channel_format(void);

/**
 * Write mono samples to I2S
 *
 * @param buffer Buffer containing mono samples
 * @param len Length of buffer in samples
 * @return ESP_OK on success
 */
esp_err_t i2s_write_mono_samples(int16_t* buffer, size_t len);

/**
 * Write stereo samples to I2S
 *
 * @param buffer Buffer containing stereo samples
 * @param len Length of buffer in stereo samples (pairs)
 * @return ESP_OK on success
 */
esp_err_t i2s_write_stereo_samples(int16_t* buffer, size_t len);

/**
 * Process audio channels
 *
 * @param buffer Audio buffer
 * @param len Buffer length
 * @return ESP_OK on success
 */
esp_err_t i2s_process_channels(int16_t* buffer, size_t len);

/**
 * Convert stereo samples to mono
 *
 * @param stereo_buffer Input stereo buffer
 * @param mono_buffer Output mono buffer
 * @param frames Number of stereo frames
 * @return ESP_OK on success
 */
esp_err_t i2s_convert_stereo_to_mono(int16_t* stereo_buffer, int16_t* mono_buffer, size_t frames);

/**
 * Convert mono samples to stereo
 *
 * @param mono_buffer Input mono buffer
 * @param stereo_buffer Output stereo buffer
 * @param mono_samples Number of mono samples
 * @return ESP_OK on success
 */
esp_err_t i2s_convert_mono_to_stereo(int16_t* mono_buffer, int16_t* stereo_buffer, size_t mono_samples);

#ifdef __cplusplus
}
#endif

#endif // I2S_AUDIO_H
