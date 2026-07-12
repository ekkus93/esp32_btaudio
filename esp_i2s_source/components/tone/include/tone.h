/*
 * tone — controllable test-tone source (WEB-1d). Provides s16 stereo samples on
 * demand via tone_fill(); the audio_out arbiter (RADIO-2c, in main) calls it
 * when radio isn't the active source. On/off + frequency are driven by the web
 * UI (/api/tone) and console. Off = silence.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TONE_HZ_MIN     20
#define TONE_HZ_MAX     20000
#define TONE_HZ_DEFAULT 440

/* Voice/timbre: a plain sine (test tone) or an additive piano-ish voice with a
 * struck-string envelope (for the on-screen piano/arpeggios). */
#define TONE_VOICE_SINE  0
#define TONE_VOICE_PIANO 1

/* Fill `frames` interleaved stereo s16 frames: a sine at the current frequency
 * when enabled, otherwise silence. Phase is carried across calls. */
void tone_fill(int16_t *out, size_t frames);

/* Enable the tone at freq_hz (clamped to [TONE_HZ_MIN, TONE_HZ_MAX]). */
void tone_set(int freq_hz);

/* Set the tone amplitude as a percent of full scale (0..100, clamped).
 * Independent of on/off and frequency; default 30%. */
void tone_set_amplitude(int pct);

/* Select the voice: TONE_VOICE_SINE (default) or TONE_VOICE_PIANO. Applies to
 * subsequent tone_set() notes. */
void tone_set_voice(int voice);

/* Disable — tone_fill() emits silence. */
void tone_off(void);

/* Current state. Either pointer may be NULL. */
void tone_get(bool *enabled, int *freq_hz);

#ifdef __cplusplus
}
#endif
