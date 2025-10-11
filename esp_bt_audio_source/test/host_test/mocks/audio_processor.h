#ifndef _AUDIO_PROCESSOR_H_
#define _AUDIO_PROCESSOR_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Minimal types used by commands.c tests
typedef enum {
    AUDIO_SAMPLE_RATE_44K = 44100
} audio_sample_rate_t;

typedef enum {
    AUDIO_BIT_DEPTH_16 = 16
} audio_bit_depth_t;

typedef struct {
    bool initialized;
    bool running;
    uint8_t volume;
} audio_status_t;

// Minimal function stubs used by host tests
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

static inline esp_err_t audio_processor_set_i2s_pins(int bclk, int ws, int din, int dout) {
    (void)bclk; (void)ws; (void)din; (void)dout;
    return ESP_OK;
}

#endif /* _AUDIO_PROCESSOR_H_ */
