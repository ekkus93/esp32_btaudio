#ifndef I2S_AUDIO_H
#define I2S_AUDIO_H

#include "esp_err.h"
#include "driver/i2s.h"

/**
 * Initialize I2S driver
 *
 * @param sample_rate Sample rate in Hz
 * @param bits_per_sample Bit depth (I2S_BITS_PER_SAMPLE_16BIT, etc.)
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
 * Configure I2S in standard mode
 *
 * @return ESP_OK on success
 */
esp_err_t i2s_configure_standard_mode(void);

/**
 * Write samples to I2S
 *
 * @param buffer Audio sample buffer
 * @param len Buffer length in samples
 * @param bytes_written Pointer to store number of bytes written
 * @return ESP_OK on success
 */
esp_err_t i2s_write_samples(int16_t* buffer, size_t len, size_t* bytes_written);

/**
 * Configure I2S for mono operation
 *
 * @param sample_rate Sample rate
 * @param bits_per_sample Bit depth
 * @return ESP_OK on success
 */
esp_err_t i2s_config_mono(int sample_rate, i2s_bits_per_sample_t bits_per_sample);

/**
 * Configure I2S for stereo operation
 *
 * @param sample_rate Sample rate
 * @param bits_per_sample Bit depth
 * @return ESP_OK on success
 */
esp_err_t i2s_config_stereo(int sample_rate, i2s_bits_per_sample_t bits_per_sample);

/**
 * Get current channel format
 *
 * @return Channel format
 */
i2s_channel_fmt_t i2s_get_channel_format(void);

/**
 * Write mono samples to I2S
 *
 * @param buffer Mono sample buffer
 * @param len Buffer length in samples
 * @return ESP_OK on success
 */
esp_err_t i2s_write_mono_samples(int16_t* buffer, size_t len);

/**
 * Write stereo samples to I2S
 *
 * @param buffer Stereo sample buffer (interleaved L/R)
 * @param len Buffer length in frames
 * @return ESP_OK on success
 */
esp_err_t i2s_write_stereo_samples(int16_t* buffer, size_t len);

/**
 * Process I2S channels
 *
 * @param buffer Sample buffer
 * @param len Buffer length in samples
 * @return ESP_OK on success
 */
esp_err_t i2s_process_channels(int16_t* buffer, size_t len);

/**
 * Convert stereo to mono
 *
 * @param stereo_buffer Stereo buffer (interleaved L/R)
 * @param mono_buffer Mono buffer
 * @param frames Number of stereo frames
 * @return ESP_OK on success
 */
esp_err_t i2s_convert_stereo_to_mono(int16_t* stereo_buffer, int16_t* mono_buffer, size_t frames);

/**
 * Convert mono to stereo
 *
 * @param mono_buffer Mono buffer
 * @param stereo_buffer Stereo buffer (interleaved L/R)
 * @param mono_samples Number of mono samples
 * @return ESP_OK on success
 */
esp_err_t i2s_convert_mono_to_stereo(int16_t* mono_buffer, int16_t* stereo_buffer, size_t mono_samples);

#endif // I2S_AUDIO_H
