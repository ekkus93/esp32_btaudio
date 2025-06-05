#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// Audio sample rates
typedef enum {
    AUDIO_SAMPLE_RATE_8K = 8000,
    AUDIO_SAMPLE_RATE_16K = 16000,
    AUDIO_SAMPLE_RATE_22K = 22050,
    AUDIO_SAMPLE_RATE_44K = 44100,
    AUDIO_SAMPLE_RATE_48K = 48000
} audio_sample_rate_t;

// Audio bit depths
typedef enum {
    AUDIO_BIT_DEPTH_16 = 16,
    AUDIO_BIT_DEPTH_24 = 24,
    AUDIO_BIT_DEPTH_32 = 32
} audio_bit_depth_t;

// Audio channels
typedef enum {
    AUDIO_CHANNEL_MONO = 1,
    AUDIO_CHANNEL_STEREO = 2
} audio_channel_t;

// Audio configuration structure
typedef struct {
    audio_sample_rate_t sample_rate;
    audio_bit_depth_t bit_depth;
    audio_channel_t channels;
    int volume;
    bool mute;
    int i2s_port;
} audio_config_t;

// Audio statistics
typedef struct {
    uint32_t current_buffer_level;
    uint32_t peak_buffer_level;
    uint32_t buffer_underruns;
    uint32_t buffer_overruns;
} audio_stats_t;

// Audio processor APIs - stub implementations for building
esp_err_t audio_processor_init(const audio_config_t* config);
esp_err_t audio_processor_deinit(void);
esp_err_t audio_processor_start(void);
esp_err_t audio_processor_stop(void);
esp_err_t audio_processor_read(void* buffer, size_t size, size_t* bytes_read);
esp_err_t audio_processor_get_stats(audio_stats_t* stats);
esp_err_t audio_processor_set_volume(int volume);
esp_err_t audio_processor_set_mute(bool mute);
esp_err_t audio_processor_set_sample_rate(audio_sample_rate_t sample_rate);
esp_err_t audio_processor_set_bit_depth(audio_bit_depth_t bit_depth);
esp_err_t audio_processor_get_config(audio_config_t* config);
