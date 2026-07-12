/* tone — controllable test-tone fill source (WEB-1d / RADIO-2c). See tone.h. */
#include "tone.h"

#include <stdatomic.h>

#include "signal_gen.h"

#define TONE_AMP_DEFAULT  30  /* percent; modest level, comfortable in earbuds */

static _Atomic bool s_on = false;               /* default off; enable via /api/tone */
static _Atomic int  s_hz = TONE_HZ_DEFAULT;
static _Atomic int  s_amp_pct = TONE_AMP_DEFAULT; /* 0..100 -> 0.0..1.0 amplitude */
static sg_sine_state_t s_sine;                  /* phase carried across fills */

void tone_fill(int16_t *out, size_t frames)
{
    double amp = atomic_load(&s_on) ? atomic_load(&s_amp_pct) / 100.0 : 0.0;
    /* Off => amplitude 0 but still advance phase so re-enabling is glitch-free. */
    sg_sine_fill(&s_sine, out, frames, (double)atomic_load(&s_hz), amp);
}

void tone_set(int freq_hz)
{
    if (freq_hz < TONE_HZ_MIN) freq_hz = TONE_HZ_MIN;
    if (freq_hz > TONE_HZ_MAX) freq_hz = TONE_HZ_MAX;
    atomic_store(&s_hz, freq_hz);
    atomic_store(&s_on, true);
}

void tone_set_amplitude(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    atomic_store(&s_amp_pct, pct);
}

void tone_off(void)
{
    atomic_store(&s_on, false);
}

void tone_get(bool *enabled, int *freq_hz)
{
    if (enabled) *enabled = atomic_load(&s_on);
    if (freq_hz) *freq_hz = atomic_load(&s_hz);
}
