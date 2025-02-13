#ifndef RADIO_H
#define RADIO_H

#include "esp_err.h"
#include "esp_http_client.h"  // Added to support HTTP streaming
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts playing an Internet radio stream from the provided URL.
esp_err_t radio_play(const char *url);

// Sets the radio active state.
void radio_set_active(bool active);

// Reads PCM data into the provided buffer.
size_t radio_read_pcm(uint8_t *dest, size_t len);

// Stops the currently playing radio stream.
esp_err_t radio_stop(void);

// Called by the decoder to write PCM samples into a ring buffer.
void radio_write_pcm(const int16_t *samples, int num_samples);

// Task function for the radio.
void radio_task(void *params);

typedef struct {
    union {
        uint8_t *data;  // For memory buffer playback
        FILE *fp;       // For file streaming
    };
    size_t size;
} radio_task_params_t;

#ifdef __cplusplus
}
#endif

#endif // RADIO_H
