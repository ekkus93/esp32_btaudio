#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "audio_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BEEP_STATE_STOPPED = 0,
    BEEP_STATE_PLAYING = 1,
} beep_state_t;

typedef struct {
    uint32_t duration_ms; /* requested duration in milliseconds */
    double freq_hz;       /* tone frequency (Hz) */
    uint16_t amplitude;   /* peak amplitude for 16-bit samples; scaled for 32-bit */
} beep_request_t;

typedef void (*beep_done_cb_t)(void *ctx);

esp_err_t beep_manager_init(void);
void beep_manager_deinit(void);

/* Optional callback invoked when a play request finishes (natural or stopped). */
void beep_manager_set_done_callback(beep_done_cb_t cb, void *ctx);

/* Start a beep using the supplied audio format; returns ESP_ERR_INVALID_STATE if already playing. */
esp_err_t beep_manager_play(const beep_request_t *req, const audio_config_t *cfg);

/* Request the current beep to stop generating/enqueueing audio immediately. */
void beep_manager_stop(void);

beep_state_t beep_manager_get_state(void);

#ifdef __cplusplus
}
#endif
