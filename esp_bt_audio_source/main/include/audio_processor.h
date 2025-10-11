#ifndef _AUDIO_PROCESSOR_H_
#define _AUDIO_PROCESSOR_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

/* When building against ESP-IDF the platform headers provide i2s types.
 * Host/unit tests (and desktop builds) don't have those headers, so provide
 * lightweight fallbacks to allow compiling the public header for tests.
 */
#ifdef ESP_PLATFORM
#include "driver/i2s_std.h"  // Use the current standard I2S driver instead of deprecated one
#else
/* Minimal host-side substitutes used only for building unit tests */
typedef int i2s_port_t;
#ifndef GPIO_NUM_NC
#define GPIO_NUM_NC (-1)
#endif
#endif

// Audio bit depths
typedef enum {
    AUDIO_BIT_DEPTH_16 = 16,
    AUDIO_BIT_DEPTH_24 = 24,
    AUDIO_BIT_DEPTH_32 = 32
} audio_bit_depth_t;

// Audio sample rates
typedef enum {
    AUDIO_SAMPLE_RATE_8K = 8000,
    AUDIO_SAMPLE_RATE_16K = 16000,
    AUDIO_SAMPLE_RATE_22K = 22050,
    AUDIO_SAMPLE_RATE_32K = 32000,
    AUDIO_SAMPLE_RATE_44K = 44100,
    AUDIO_SAMPLE_RATE_48K = 48000,
    AUDIO_SAMPLE_RATE_96K = 96000
} audio_sample_rate_t;

// Audio channel modes
typedef enum {
    AUDIO_CHANNEL_MONO = 1,
    AUDIO_CHANNEL_STEREO = 2
} audio_channel_t;

// Audio configuration
typedef struct {
    audio_sample_rate_t sample_rate;
    audio_bit_depth_t bit_depth;
    audio_channel_t channels;
    uint8_t volume;        // 0-100%
    bool mute;
    i2s_port_t i2s_port;  // I2S port number
    /* Optional I2S pin configuration (GPIO numbers). Use GPIO_NUM_NC for unused pins. */
    int i2s_bclk_pin;
    int i2s_ws_pin;
    int i2s_din_pin;
    int i2s_dout_pin;
} audio_config_t;

// Audio statistics
typedef struct {
    uint32_t samples_processed;
    uint32_t buffer_overruns;
    uint32_t buffer_underruns;
    uint32_t conversion_errors;
    float cpu_load;               // Percentage 0-1
    uint32_t current_buffer_level;
    uint32_t peak_buffer_level;
} audio_stats_t;

/** 
 * @brief Initialize the audio processor
 * 
 * @param config Audio configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_init(const audio_config_t* config);

/**
 * @brief Deinitialize the audio processor
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_deinit(void);

/**
 * @brief Start audio processing
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_start(void);

/**
 * @brief Stop audio processing
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_stop(void);

/**
 * @brief Set the output sample rate
 * 
 * @param sample_rate New sample rate
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_set_sample_rate(audio_sample_rate_t sample_rate);

/**
 * @brief Set the output bit depth
 * 
 * @param bit_depth New bit depth
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_set_bit_depth(audio_bit_depth_t bit_depth);

/**
 * @brief Set audio volume
 * 
 * @param volume Volume level (0-100%)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_set_volume(uint8_t volume);

/**
 * @brief Mute or unmute audio
 * 
 * @param mute true to mute, false to unmute
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_set_mute(bool mute);

/**
 * @brief Get current audio configuration
 * 
 * @param config Pointer to configuration structure to fill
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_get_config(audio_config_t* config);

/**
 * @brief Get audio processing statistics
 * 
 * @param stats Pointer to statistics structure to fill
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_get_stats(audio_stats_t* stats);

/**
 * @brief Read processed audio data
 * 
 * @param buffer Buffer to store audio data
 * @param size Size of the buffer
 * @param bytes_read Number of bytes read
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_read(uint8_t* buffer, size_t size, size_t* bytes_read);

/**
 * @brief Simple audio runtime status
 */
typedef struct {
    bool initialized;
    bool running;
    uint8_t volume;    // 0-100
    bool mute;
    audio_sample_rate_t sample_rate;
    audio_bit_depth_t bit_depth;
    audio_channel_t channels;
} audio_status_t;

/**
 * @brief Get a compact runtime status of the audio processor
 */
esp_err_t audio_processor_get_status(audio_status_t* status);

/**
 * @brief Set I2S pins (bclk/ws/din[,dout]) at runtime.
 * If the audio processor is running it will be stopped and restarted to apply pins.
 */
esp_err_t audio_processor_set_i2s_pins(int bclk_pin, int ws_pin, int din_pin, int dout_pin);

#endif /* _AUDIO_PROCESSOR_H_ */
