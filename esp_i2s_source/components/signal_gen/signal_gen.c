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
