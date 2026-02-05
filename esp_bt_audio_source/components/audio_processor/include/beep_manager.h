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
void beep_manager_set_done_callback(beep_done_cb_t callback, void *ctx);

/**
 * Beep overlay fill API for ring buffer architecture (CODE_REVIEW6 Phase 3.3)
 * 
 * Mixes beep samples into existing buffer in-place. Uses internal state to track
 * beep progress (phase, remaining frames, fade envelope).
 * 
 * @param buffer Buffer containing base audio (modified in-place)
 * @param bytes Buffer size in bytes
 * @param cfg Audio configuration (bit depth, channels, sample rate)
 * 
 * WHY: Ring buffer architecture needs in-place mixing instead of separate enqueue
 * HOW: Generate beep samples, mix with base using clamped add (base*0.7 + beep*0.5)
 * CORRECTNESS: Thread-safe via state lock, auto-stops when duration exhausted
 */
void beep_overlay_fill(uint8_t *buffer, size_t bytes, const audio_config_t *cfg);

/**
 * Start beep overlay with specified parameters (CODE_REVIEW6 Phase 3.3)
 * 
 * @param req Beep parameters (duration, frequency, amplitude)
 * @param cfg Audio configuration (must match ring buffer format)
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already playing
 */
esp_err_t beep_overlay_start(const beep_request_t *req, const audio_config_t *cfg);

/**
 * Check if beep overlay is currently active
 * @return true if beep is generating, false otherwise
 */
bool beep_overlay_is_active(void);
void beep_overlay_stop(void);

/* Start a beep using the supplied audio format; returns ESP_ERR_INVALID_STATE if already playing. */
esp_err_t beep_manager_play(const beep_request_t *req, const audio_config_t *cfg);

/* Request the current beep to stop generating immediately. */
void beep_manager_stop(void);

beep_state_t beep_manager_get_state(void);

#ifdef __cplusplus
}
#endif
