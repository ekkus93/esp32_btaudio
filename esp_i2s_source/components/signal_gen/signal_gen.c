/*
 * signal_gen — sine / sweep / silence producers (SIG-1a). Pure sample math;
 * no ESP-IDF dependencies so it host-tests directly. See signal_gen.h.
 */
#include "signal_gen.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TWO_PI (2.0 * M_PI)
#define S16_MAX 32767.0

static inline int16_t sample_from_phase(double phase, double amplitude)
{
    if (amplitude < 0.0) amplitude = 0.0;
    if (amplitude > 1.0) amplitude = 1.0;
    double v = amplitude * S16_MAX * sin(phase);
    long s = lround(v);
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    return (int16_t)s;
}

static inline double wrap_phase(double phase)
{
    /* Keep phase bounded so it never loses precision over long runs. */
    while (phase >= TWO_PI) phase -= TWO_PI;
    while (phase < 0.0) phase += TWO_PI;
    return phase;
}

void sg_silence_fill(int16_t *out, size_t frames)
{
    memset(out, 0, frames * SIGNAL_GEN_CHANNELS * sizeof(int16_t));
}

void sg_sine_reset(sg_sine_state_t *st)
{
    st->phase = 0.0;
}

void sg_sine_fill(sg_sine_state_t *st, int16_t *out, size_t frames,
                  double freq_hz, double amplitude)
{
    const double dphi = TWO_PI * freq_hz / (double)SIGNAL_GEN_SAMPLE_RATE_HZ;
    double phase = st->phase;
    for (size_t f = 0; f < frames; f++) {
        int16_t s = sample_from_phase(phase, amplitude);
        out[f * 2] = s;
        out[f * 2 + 1] = s;
        phase = wrap_phase(phase + dphi);
    }
    st->phase = phase;
}

/* --- Piano/keyboard voice: band-limited sawtooth + struck-string envelope ---
 * A single sawtooth (all harmonics, 1/n rolloff — more texture than a sine,
 * simpler and cheaper than a big additive stack) with a fast attack and
 * exponential decay so each note is struck and rings down. PolyBLEP smooths the
 * wrap discontinuity so the higher notes don't alias/screech. */
#define PIANO_TAU_S       0.9    /* decay time constant (seconds) */
#define PIANO_ATTACK_S    0.003  /* 3 ms attack ramp (percussive) */
#define PIANO_ENV_BLOCK   32     /* re-evaluate the envelope every N samples */

void sg_piano_note_on(sg_piano_state_t *st)
{
    st->phase = 0.0;   /* normalized phase [0, 1) for the sawtooth */
    st->elapsed = 0;
}

/* PolyBLEP correction at the sawtooth's [0,1) wrap — suppresses alias energy. */
static inline double poly_blep(double t, double dt)
{
    if (t < dt) { t /= dt; return (t + t) - (t * t) - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (t * t) + (t + t) + 1.0; }
    return 0.0;
}

void sg_piano_fill(sg_piano_state_t *st, int16_t *out, size_t frames,
                   double freq_hz, double amplitude)
{
    if (amplitude < 0.0) amplitude = 0.0;
    if (amplitude > 1.0) amplitude = 1.0;
    const double dt = freq_hz / (double)SIGNAL_GEN_SAMPLE_RATE_HZ; /* phase step */

    double phase = st->phase;
    uint32_t elapsed = st->elapsed;
    size_t f = 0;
    while (f < frames) {
        /* Envelope for this sub-block: fast attack, then exponential decay. */
        const double t = (double)elapsed / (double)SIGNAL_GEN_SAMPLE_RATE_HZ;
        const double attack = (t < PIANO_ATTACK_S) ? (t / PIANO_ATTACK_S) : 1.0;
        const double scale = amplitude * attack * exp(-t / PIANO_TAU_S);

        size_t blk = frames - f;
        if (blk > PIANO_ENV_BLOCK) blk = PIANO_ENV_BLOCK;
        for (size_t k = 0; k < blk; k++) {
            double saw = (2.0 * phase - 1.0) - poly_blep(phase, dt);
            long s = lround(scale * saw * S16_MAX);
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            out[(f + k) * 2] = (int16_t)s;
            out[(f + k) * 2 + 1] = (int16_t)s;
            phase += dt;
            if (phase >= 1.0) phase -= 1.0;
        }
        elapsed += (uint32_t)blk;
        f += blk;
    }
    st->phase = phase;
    st->elapsed = elapsed;
}

void sg_sweep_reset(sg_sweep_state_t *st)
{
    st->phase = 0.0;
    st->t = 0.0;
}

void sg_sweep_fill(sg_sweep_state_t *st, int16_t *out, size_t frames,
                   double f0_hz, double f1_hz, double duration_s,
                   double amplitude)
{
    const double dt = 1.0 / (double)SIGNAL_GEN_SAMPLE_RATE_HZ;
    const double rate = (duration_s > 0.0) ? (f1_hz - f0_hz) / duration_s : 0.0;
    double phase = st->phase;
    double t = st->t;
    for (size_t f = 0; f < frames; f++) {
        double freq = f0_hz + rate * t;
        if (rate >= 0.0) { if (freq > f1_hz) freq = f1_hz; }
        else            { if (freq < f1_hz) freq = f1_hz; }
        int16_t s = sample_from_phase(phase, amplitude);
        out[f * 2] = s;
        out[f * 2 + 1] = s;
        phase = wrap_phase(phase + TWO_PI * freq * dt);
        t += dt;
    }
    st->phase = phase;
    st->t = t;
}
