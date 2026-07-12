/* tone — controllable test-tone fill source (WEB-1d / RADIO-2c). See tone.h. */
#include "tone.h"

#include <stdatomic.h>

#include "signal_gen.h"

#define TONE_AMP_DEFAULT  30  /* percent; modest level, comfortable in earbuds */

static _Atomic bool s_on = false;               /* default off; enable via /api/tone */
static _Atomic int  s_hz = TONE_HZ_DEFAULT;
static _Atomic int  s_amp_pct = TONE_AMP_DEFAULT; /* 0..100 -> 0.0..1.0 amplitude */
static _Atomic int  s_voice = TONE_VOICE_SINE;    /* sine (test tone) or piano */
static _Atomic bool s_retrigger = false;          /* piano: restart envelope next fill */
static sg_sine_state_t  s_sine;                 /* phase carried across fills */
static sg_piano_state_t s_piano;                /* piano phase + envelope clock */

void tone_fill(int16_t *out, size_t frames)
{
    if (atomic_load(&s_voice) == TONE_VOICE_PIANO) {
        /* Retrigger the envelope in the audio task (keeps s_piano single-owner). */
        if (atomic_exchange(&s_retrigger, false)) sg_piano_note_on(&s_piano);
        double amp = atomic_load(&s_on) ? atomic_load(&s_amp_pct) / 100.0 : 0.0;
        sg_piano_fill(&s_piano, out, frames, (double)atomic_load(&s_hz), amp);
        return;
    }
    double amp = atomic_load(&s_on) ? atomic_load(&s_amp_pct) / 100.0 : 0.0;
    /* Off => amplitude 0 but still advance phase so re-enabling is glitch-free. */
    sg_sine_fill(&s_sine, out, frames, (double)atomic_load(&s_hz), amp);
}

void tone_set_voice(int voice)
{
    atomic_store(&s_voice, (voice == TONE_VOICE_PIANO) ? TONE_VOICE_PIANO : TONE_VOICE_SINE);
}

void tone_set(int freq_hz)
{
    if (freq_hz < TONE_HZ_MIN) freq_hz = TONE_HZ_MIN;
    if (freq_hz > TONE_HZ_MAX) freq_hz = TONE_HZ_MAX;
    atomic_store(&s_hz, freq_hz);
    /* A fresh note restarts the piano envelope (struck each keypress). */
    if (atomic_load(&s_voice) == TONE_VOICE_PIANO) atomic_store(&s_retrigger, true);
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
