#ifndef _AUDIO_PROCESSOR_H_
#define _AUDIO_PROCESSOR_H_

#include "esp_err.h"
#include "driver/i2s.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio sample rates supported by the processor
 */
typedef enum {
    AUDIO_SAMPLE_RATE_8K = 8000,
    AUDIO_SAMPLE_RATE_16K = 16000,
    AUDIO_SAMPLE_RATE_24K = 24000,
    AUDIO_SAMPLE_RATE_32K = 32000,
    AUDIO_SAMPLE_RATE_44K = 44100,
    AUDIO_SAMPLE_RATE_48K = 48000
} audio_sample_rate_t;

/**
 * @brief Audio bit depths supported by the processor
 */
typedef enum {
    AUDIO_BIT_DEPTH_16 = 16,
    AUDIO_BIT_DEPTH_24 = 24,
    AUDIO_BIT_DEPTH_32 = 32
} audio_bit_depth_t;

/**
 * @brief Audio channels configuration
 */
typedef enum {
    AUDIO_CHANNEL_MONO = 1,
    AUDIO_CHANNEL_STEREO = 2
} audio_channel_t;

/**
 * @brief Audio processor configuration
 */
typedef struct {
    audio_sample_rate_t sample_rate;
    audio_bit_depth_t bit_depth;
    audio_channel_t channels;
    uint8_t volume;        // 0-100
    bool mute;
    i2s_port_t i2s_port;
} audio_config_t;

/**
 * @brief Audio statistics structure
 */
typedef struct {
    uint32_t current_buffer_level;  // Current buffer fill level in bytes
    uint32_t peak_buffer_level;     // Peak buffer usage since last query
    uint32_t buffer_underruns;      // Number of buffer underruns
    uint32_t buffer_overruns;       // Number of buffer overruns
    uint32_t samples_processed;     // Total samples processed
} audio_stats_t;

/**
 * Initialize the audio processor
 */
esp_err_t audio_processor_init(const audio_config_t *config);

/**
 * Deinitialize the audio processor
 */
esp_err_t audio_processor_deinit(void);

/**
 * Start audio processing
 */
esp_err_t audio_processor_start(void);

/**
 * Stop audio processing
 */
esp_err_t audio_processor_stop(void);

/**
 * Set audio volume
 */
esp_err_t audio_processor_set_volume(uint8_t volume);

/**
 * Set audio mute state
 */
esp_err_t audio_processor_set_mute(bool mute);

/**
 * Get current audio configuration
 */
esp_err_t audio_processor_get_config(audio_config_t *config);

/**
 * Set audio sample rate
 */
esp_err_t audio_processor_set_sample_rate(audio_sample_rate_t rate);

/**
 * Set audio bit depth
 */
esp_err_t audio_processor_set_bit_depth(audio_bit_depth_t depth);

/**
 * Read audio data
 */
esp_err_t audio_processor_read(uint8_t *buffer, size_t size, size_t *bytes_read);

/**
 * Get audio processing statistics
 */
esp_err_t audio_processor_get_stats(audio_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* _AUDIO_PROCESSOR_H_ */
