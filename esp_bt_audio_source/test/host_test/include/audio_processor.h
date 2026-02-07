/* Test shim for audio_processor public header.
 * This shim provides the minimal public API types and stubs required by
 * host/unit tests. It avoids modifying production headers and prevents
 * collisions with mock headers by living in the test include path which
 * is preferred by the test CMakeLists.
 */
#ifndef _AUDIO_PROCESSOR_H_
#define _AUDIO_PROCESSOR_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

// Minimal enums/types used by host tests
typedef enum {
    AUDIO_BIT_DEPTH_16 = 16,
    AUDIO_BIT_DEPTH_24 = 24,
    AUDIO_BIT_DEPTH_32 = 32
} audio_bit_depth_t;

typedef enum {
    AUDIO_SAMPLE_RATE_44K = 44100,
    AUDIO_SAMPLE_RATE_48K = 48000
} audio_sample_rate_t;

typedef enum {
    AUDIO_CHANNEL_MONO = 1,
    AUDIO_CHANNEL_STEREO = 2
} audio_channel_t;

typedef struct {
    audio_sample_rate_t sample_rate;
    audio_bit_depth_t bit_depth;
    audio_channel_t channels;
    uint8_t volume;
    bool mute;
} audio_config_t;

typedef struct {
    bool initialized;
    bool running;
    uint8_t volume;
} audio_status_t;

typedef struct {
    uint32_t samples_processed;
    uint32_t buffer_overruns;
    uint32_t buffer_underruns;
    uint32_t conversion_errors;
    float cpu_load;
    uint32_t current_buffer_level;
    uint32_t peak_buffer_level;
} audio_stats_t;

// Minimal function stubs used by host tests. Implementations in mocks
// (if needed) will override these symbols when linked. Keep inline so
// the compiler can optimize them away when not used.
static inline esp_err_t audio_processor_get_status(audio_status_t* status) {
    if (!status) return ESP_ERR_INVALID_ARG;
    status->initialized = true;
    status->running = false;
    status->volume = 75;
    return ESP_OK;
}

static inline esp_err_t audio_processor_set_volume(uint8_t volume) {
    (void)volume;
    return ESP_OK;
}

static inline bool audio_processor_is_i2s_active(void) {
    return false;
}

static inline esp_err_t audio_processor_beep_tone(uint32_t duration_ms, double freq_hz) {
    (void)duration_ms;
    (void)freq_hz;
    return ESP_OK;
}

static inline esp_err_t audio_processor_start(void) { return ESP_OK; }
static inline esp_err_t audio_processor_deinit(void) { return ESP_OK; }
static inline esp_err_t audio_processor_stop(void) { return ESP_OK; }
static inline void audio_processor_set_synth_mode(bool enable) { (void)enable; }

#endif /* _AUDIO_PROCESSOR_H_ */
