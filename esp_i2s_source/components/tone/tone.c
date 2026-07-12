/* tone — controllable test-tone fill source (WEB-1d / RADIO-2c). See tone.h. */
#include "tone.h"

#include <stdatomic.h>

#include "signal_gen.h"

#define TONE_AMPLITUDE  0.30  /* modest level, comfortable in earbuds */

static _Atomic bool s_on = false;               /* default off; enable via /api/tone */
static _Atomic int  s_hz = TONE_HZ_DEFAULT;
static sg_sine_state_t s_sine;                  /* phase carried across fills */

void tone_fill(int16_t *out, size_t frames)
{
    if (atomic_load(&s_on)) {
        sg_sine_fill(&s_sine, out, frames, (double)atomic_load(&s_hz), TONE_AMPLITUDE);
    } else {
        /* Advance phase at 0 amplitude so re-enabling is glitch-free. */
        sg_sine_fill(&s_sine, out, frames, (double)atomic_load(&s_hz), 0.0);
    }
}

void tone_set(int freq_hz)
{
    if (freq_hz < TONE_HZ_MIN) freq_hz = TONE_HZ_MIN;
    if (freq_hz > TONE_HZ_MAX) freq_hz = TONE_HZ_MAX;
    atomic_store(&s_hz, freq_hz);
    atomic_store(&s_on, true);
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
