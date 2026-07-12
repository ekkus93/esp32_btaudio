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

/* --- Piano-ish voice (additive harmonics + struck-string envelope) --- */

#define PIANO_HARMONICS   5
#define PIANO_TAU_S       1.2    /* fundamental decay time constant (seconds) */
#define PIANO_ATTACK_S    0.004  /* 4 ms attack ramp (percussive) */

/* Relative harmonic weights (fundamental, 2nd, 3rd, ...). */
static const double PIANO_W[PIANO_HARMONICS] = {1.0, 0.5, 0.30, 0.16, 0.08};

void sg_piano_note_on(sg_piano_state_t *st)
{
    st->phase = 0.0;
    st->elapsed = 0;
}

/* Envelope re-evaluated every PIANO_ENV_BLOCK samples (~0.7 ms) so it stays
 * smooth for any fill size, while keeping exp() calls negligible. */
#define PIANO_ENV_BLOCK 32

void sg_piano_fill(sg_piano_state_t *st, int16_t *out, size_t frames,
                   double freq_hz, double amplitude)
{
    if (amplitude < 0.0) amplitude = 0.0;
    if (amplitude > 1.0) amplitude = 1.0;
    const double nyquist = SIGNAL_GEN_SAMPLE_RATE_HZ / 2.0;
    const double dphi = TWO_PI * freq_hz / (double)SIGNAL_GEN_SAMPLE_RATE_HZ;

    double norm = 0.0;
    for (int n = 0; n < PIANO_HARMONICS; n++) norm += PIANO_W[n];
    if (norm <= 0.0) norm = 1.0;

    double phase = st->phase;
    uint32_t elapsed = st->elapsed;
    size_t f = 0;
    while (f < frames) {
        /* Envelope for this sub-block: fast attack, then per-harmonic decay
         * (higher harmonics fade faster -> bright attack, mellow ring). */
        const double t = (double)elapsed / (double)SIGNAL_GEN_SAMPLE_RATE_HZ;
        const double attack = (t < PIANO_ATTACK_S) ? (t / PIANO_ATTACK_S) : 1.0;
        double gain[PIANO_HARMONICS];
        for (int n = 0; n < PIANO_HARMONICS; n++) {
            if (freq_hz * (n + 1) >= nyquist) { gain[n] = 0.0; continue; }
            gain[n] = PIANO_W[n] * exp(-t / (PIANO_TAU_S / (double)(n + 1)));
        }
        const double scale = amplitude * attack / norm;

        size_t blk = frames - f;
        if (blk > PIANO_ENV_BLOCK) blk = PIANO_ENV_BLOCK;
        for (size_t k = 0; k < blk; k++) {
            double v = 0.0;
            for (int n = 0; n < PIANO_HARMONICS; n++) {
                if (gain[n] != 0.0) v += gain[n] * sin((double)(n + 1) * phase);
            }
            long s = lround(scale * v * S16_MAX);
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            out[(f + k) * 2] = (int16_t)s;
            out[(f + k) * 2 + 1] = (int16_t)s;
            phase = wrap_phase(phase + dphi);
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
