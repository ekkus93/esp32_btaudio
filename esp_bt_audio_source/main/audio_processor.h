#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2s_std.h"

/**
 * @brief Audio sample rates supported by the processor
 */
typedef enum {
    AUDIO_SAMPLE_RATE_8K = 8000,
    AUDIO_SAMPLE_RATE_16K = 16000,
    AUDIO_SAMPLE_RATE_22K = 22050,
    AUDIO_SAMPLE_RATE_32K = 32000,
    AUDIO_SAMPLE_RATE_44K = 44100,
    AUDIO_SAMPLE_RATE_48K = 48000,
} audio_sample_rate_t;

/**
 * @brief Audio bit depths supported by the processor
 */
typedef enum {
    AUDIO_BIT_DEPTH_16 = 16,
    AUDIO_BIT_DEPTH_24 = 24,
    AUDIO_BIT_DEPTH_32 = 32,
} audio_bit_depth_t;

/**
 * @brief Audio channel configurations
 */
typedef enum {
    AUDIO_CHANNEL_MONO = 1,
    AUDIO_CHANNEL_STEREO = 2,
} audio_channels_t;

/**
 * @brief Audio processing configuration
 */
typedef struct {
    audio_sample_rate_t sample_rate;     // Sample rate in Hz
    audio_bit_depth_t bit_depth;         // Bits per sample
    audio_channels_t channels;           // Number of channels
    uint8_t volume;                      // Volume level (0-100)
    bool mute;                           // Mute state
    i2s_port_t i2s_port;                 // I2S port number
} audio_config_t;

/**
 * @brief Audio processing statistics
 */
typedef struct {
    uint32_t samples_processed;          // Total samples processed
    uint32_t buffer_underruns;           // Buffer underrun count
    uint32_t buffer_overruns;            // Buffer overrun count
    uint32_t current_buffer_level;       // Current buffer fill level
    uint32_t peak_buffer_level;          // Peak buffer fill level
    float cpu_load;                      // Approximate CPU load (0-1.0)
    uint32_t input_format_errors;        // Input format errors
    uint32_t conversion_errors;          // Format conversion errors
} audio_stats_t;

/**
 * @brief Initialize the audio processor
 * @param config Pointer to audio configuration
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t audio_processor_init(const audio_config_t* config);

/**
 * @brief Deinitialize the audio processor and free resources
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t audio_processor_deinit(void);

/**
 * @brief Start audio processing
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t audio_processor_start(void);

/**
 * @brief Stop audio processing
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t audio_processor_stop(void);

/**
 * @brief Set the output sample rate
 * @param sample_rate New sample rate
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t audio_processor_set_sample_rate(audio_sample_rate_t sample_rate);

/**
 * @brief Set audio volume
 * @param volume Volume level (0-100)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t audio_processor_set_volume(uint8_t volume);

/**
 * @brief Mute or unmute audio
 * @param mute true to mute, false to unmute
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t audio_processor_set_mute(bool mute);

/**
 * @brief Set the audio bit depth
 * @param bit_depth New bit depth
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t audio_processor_set_bit_depth(audio_bit_depth_t bit_depth);

/**
 * @brief Get current audio configuration
 * @param config Pointer to store configuration
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t audio_processor_get_config(audio_config_t* config);

/**
 * @brief Get audio processing statistics
 * @param stats Pointer to store statistics
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t audio_processor_get_stats(audio_stats_t* stats);

/**
 * @brief Read processed audio data
 * @param buffer Buffer to store audio data
 * @param size Size of buffer in bytes
 * @param bytes_read Pointer to store actual bytes read
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t audio_processor_read(void* buffer, size_t size, size_t* bytes_read);

#endif /* AUDIO_PROCESSOR_H */
