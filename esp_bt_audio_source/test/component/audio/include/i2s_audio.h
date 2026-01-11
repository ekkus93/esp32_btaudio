#ifndef I2S_AUDIO_H
#define I2S_AUDIO_H

#include "esp_err.h"
// Fix the include path - the component name is automatically handled by the include system
#include <stddef.h>
#include "driver/i2s_std.h" // Modern I2S API
#include "freertos/FreeRTOS.h" // For portMAX_DELAY
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Our channel handle - globally accessible for components that need it
extern i2s_chan_handle_t i2s_tx_handle;

/**
 * Initialize the I2S driver with given parameters using the modern channel-based API
 * 
 * @param sample_rate Sample rate in Hz (e.g., 44100)
 * @param bits_per_sample Bits per sample (I2S_DATA_BIT_WIDTH_16BIT, I2S_DATA_BIT_WIDTH_24BIT, etc)
 * @param channel_fmt Channel format (I2S_SLOT_MODE_STEREO, I2S_SLOT_MODE_MONO)
 * @return ESP_OK on success, error otherwise
 */
esp_err_t i2s_driver_init(int sample_rate, i2s_data_bit_width_t bits_per_sample, i2s_slot_mode_t slot_mode);

/**
 * Properly deinitialize the I2S driver
 */
esp_err_t i2s_driver_deinit(void);

/**
 * Check if driver is installed
 */
bool i2s_is_driver_installed(void);

/**
 * Configure I2S in standard audio mode (44.1kHz, 16-bit, stereo)
 * @return ESP_OK on success, error otherwise
 */
esp_err_t i2s_configure_standard_mode(void);

/**
 * Write samples to I2S
 * 
 * @param buffer Buffer containing samples
 * @param len Length of buffer in samples
 * @param bytes_written Pointer to store number of bytes written
 * @return ESP_OK on success, error otherwise
 */
esp_err_t i2s_write_samples(int16_t* buffer, size_t len, size_t* bytes_written);

/**
 * Configure mono mode
 * 
 * @param sample_rate Sample rate in Hz
 * @param bits_per_sample Bits per sample
 * @return ESP_OK on success, error otherwise
 */
esp_err_t i2s_config_mono(int sample_rate, i2s_data_bit_width_t bits_per_sample);

/**
 * Configure stereo mode
 * 
 * @param sample_rate Sample rate in Hz
 * @param bits_per_sample Bits per sample
 * @return ESP_OK on success, error otherwise
 */
esp_err_t i2s_config_stereo(int sample_rate, i2s_data_bit_width_t bits_per_sample);

/**
 * Get current channel format
 * 
 * @return Current channel format (stereo/mono)
 */
i2s_slot_mode_t i2s_get_channel_format(void);

/**
 * Write mono samples
 * 
 * @param buffer Buffer containing mono samples
 * @param len Length of buffer in samples
 * @return ESP_OK on success, error otherwise
 */
esp_err_t i2s_write_mono_samples(int16_t* buffer, size_t len);

/**
 * Write stereo samples
 * 
 * @param buffer Buffer containing stereo samples
 * @param len Length of buffer in samples
 * @return ESP_OK on success, error otherwise
 */
esp_err_t i2s_write_stereo_samples(int16_t* buffer, size_t len);

/**
 * Process channels
 * 
 * @param buffer Buffer containing samples
 * @param len Length of buffer in samples
 * @return ESP_OK on success, error otherwise
 */
esp_err_t i2s_process_channels(int16_t* buffer, size_t len);

/**
 * Convert stereo to mono
 * 
 * @param stereo_buffer Stereo input buffer (left, right, left, right...)
 * @param mono_buffer Mono output buffer
 * @param frames Number of stereo frames (1 frame = 1 left + 1 right sample)
 * @return ESP_OK on success, error otherwise
 */
esp_err_t i2s_convert_stereo_to_mono(int16_t* stereo_buffer, int16_t* mono_buffer, size_t frames);

/**
 * Convert mono to stereo
 * 
 * @param mono_buffer Mono input buffer
 * @param stereo_buffer Stereo output buffer
 * @param mono_samples Number of mono samples
 * @return ESP_OK on success, error otherwise
 */
esp_err_t i2s_convert_mono_to_stereo(int16_t* mono_buffer, int16_t* stereo_buffer, size_t mono_samples);

#ifdef __cplusplus
}
#endif

#endif /* I2S_AUDIO_H */
