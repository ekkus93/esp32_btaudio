/*
 * tone — controllable 440 Hz-style test tone source feeding i2s_out (WEB-1d).
 * Replaces main.c's hardcoded always-on tone_task with an on/off + frequency
 * control the web UI (/api/tone) and console can drive. When off, it emits
 * silence rather than stopping (the S3 is the I2S slave-TX and must keep the
 * WROOM32 master's stream fed). This is a stopgap single-source model; real
 * source arbitration (radio ⇄ tone ⇄ silence) arrives at RADIO-2c.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TONE_HZ_MIN   20
#define TONE_HZ_MAX   20000
#define TONE_HZ_DEFAULT 440

/* Spawn the tone writer task (feeds i2s_out). i2s_out must be started. */
esp_err_t tone_start(void);

/* Enable the tone at freq_hz (clamped to [TONE_HZ_MIN, TONE_HZ_MAX]). */
void tone_set(int freq_hz);

/* Disable — emit silence (the I2S stream keeps flowing). */
void tone_off(void);

/* Current state. Either pointer may be NULL. */
void tone_get(bool *enabled, int *freq_hz);

#ifdef __cplusplus
}
#endif
