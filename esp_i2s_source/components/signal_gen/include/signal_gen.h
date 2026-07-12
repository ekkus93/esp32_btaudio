/*
 * signal_gen — sine / sweep / silence producers at 44.1 kHz stereo s16
 * (SPEC §4). Pure, host-testable sample math with phase carried across calls
 * so successive fills are glitch-free. The audio path's built-in diagnostic
 * and the Phase-1 source (SIG-1a).
 *
 * Buffer convention: `out` holds interleaved stereo frames, length
 * `frames * SIGNAL_GEN_CHANNELS` int16 samples; L and R carry the same value.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#define SIGNAL_GEN_SAMPLE_RATE_HZ 44100
#define SIGNAL_GEN_CHANNELS       2

#ifdef __cplusplus
extern "C" {
#endif

/* --- Silence --- */
void sg_silence_fill(int16_t *out, size_t frames);

/* --- Sine --- */
typedef struct {
    double phase;   /* current phase in radians, kept in [0, 2*PI) */
} sg_sine_state_t;

void sg_sine_reset(sg_sine_state_t *st);

/* Fill `frames` stereo frames with a sine of `freq_hz` at `amplitude`
 * (0.0..1.0, clamped). Phase carried in `st`. */
void sg_sine_fill(sg_sine_state_t *st, int16_t *out, size_t frames,
                  double freq_hz, double amplitude);

/* --- Piano/keyboard voice ---
 * A band-limited (PolyBLEP) sawtooth with a struck-string envelope (fast attack,
 * exponential decay) — more texture than a sine, one simple waveform rather than
 * an additive stack, and alias-suppressed so the high notes stay clean.
 * Retrigger the envelope on each note with sg_piano_note_on(). */
typedef struct {
    double   phase;     /* fundamental phase, [0, 2*PI) */
    uint32_t elapsed;   /* samples since note-on (envelope clock) */
} sg_piano_state_t;

/* Retrigger: restart the envelope (and phase) for a fresh note. */
void sg_piano_note_on(sg_piano_state_t *st);

/* Fill `frames` with the piano voice at `freq_hz`, scaled by `amplitude`
 * (0.0..1.0). Phase + envelope clock carried in `st`. */
void sg_piano_fill(sg_piano_state_t *st, int16_t *out, size_t frames,
                   double freq_hz, double amplitude);

/* --- Linear sweep --- */
typedef struct {
    double phase;   /* radians, [0, 2*PI) */
    double t;       /* elapsed seconds since reset */
} sg_sweep_state_t;

void sg_sweep_reset(sg_sweep_state_t *st);

/* Fill `frames` with a linear-frequency sweep from `f0_hz` to `f1_hz` over
 * `duration_s`; instantaneous frequency clamps at `f1_hz` past the duration. */
void sg_sweep_fill(sg_sweep_state_t *st, int16_t *out, size_t frames,
                   double f0_hz, double f1_hz, double duration_s,
                   double amplitude);

#ifdef __cplusplus
}
#endif
