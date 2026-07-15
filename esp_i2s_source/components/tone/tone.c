/* tone — controllable test-tone fill source (WEB-1d / RADIO-2c). See tone.h.
 *
 * TODO 5.3/TONE-001: config (on/hz/amp_pct/voice) is one mutex-guarded
 * struct instead of four independently-atomic fields, so tone_fill() always
 * reads a coherent snapshot — previously the audio task could observe a mix
 * of a new frequency with the old voice/amplitude (each field updated
 * independently by a setter with no ordering guarantee between them).
 *
 * TODO 5.4: a 10 ms linear gain ramp smooths on/off and amplitude-percent
 * transitions (previously an instant step, audible as a click). The
 * underlying generator always runs at unity amplitude (so phase keeps
 * advancing regardless of on/off — glitch-free re-enable, unchanged from
 * before); the ramp is the only amplitude control applied here.
 */
#include "tone.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "signal_gen.h"

#define TONE_AMP_DEFAULT  30  /* percent; modest level, comfortable in earbuds */
#define TONE_RAMP_MS      10
#define TONE_RAMP_SAMPLES (SIGNAL_GEN_SAMPLE_RATE_HZ / 1000 * TONE_RAMP_MS)

typedef struct {
    bool on;
    int  voice;
    int  hz;
    int  amp_pct;
} tone_config_t;

static SemaphoreHandle_t s_mtx;
static tone_config_t s_config = {
    .on = false, .voice = TONE_VOICE_SINE, .hz = TONE_HZ_DEFAULT, .amp_pct = TONE_AMP_DEFAULT,
};
static _Atomic bool s_retrigger = false;   /* piano: restart envelope next fill */
static sg_sine_state_t  s_sine;            /* phase carried across fills */
static sg_piano_state_t s_piano;           /* piano phase + envelope clock */
static float s_ramp_gain = 0.0f;           /* current smoothed 0..1 multiplier */

static SemaphoreHandle_t mtx(void)
{
    /* Lazily created once; tone_fill()/setters may run before any explicit
     * init call exists for this component. */
    if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
    return s_mtx;
}

static tone_config_t snapshot_config(void)
{
    tone_config_t cfg;
    xSemaphoreTake(mtx(), portMAX_DELAY);
    cfg = s_config;
    xSemaphoreGive(mtx());
    return cfg;
}

void tone_fill(int16_t *out, size_t frames)
{
    if (!out || frames == 0) return;

    tone_config_t cfg = snapshot_config();

    if (cfg.voice == TONE_VOICE_PIANO) {
        /* Retrigger the envelope in the audio task (keeps s_piano single-owner). */
        if (atomic_exchange(&s_retrigger, false)) sg_piano_note_on(&s_piano);
        sg_piano_fill(&s_piano, out, frames, (double)cfg.hz, 1.0);
    } else {
        sg_sine_fill(&s_sine, out, frames, (double)cfg.hz, 1.0);
    }

    /* Click-suppressed overall gain: linear ramp toward the target over
     * TONE_RAMP_SAMPLES, applied per-frame so on/off and amplitude changes
     * never step instantaneously. */
    float target = cfg.on ? (float)cfg.amp_pct / 100.0f : 0.0f;
    const float step = 1.0f / (float)TONE_RAMP_SAMPLES;
    for (size_t i = 0; i < frames; i++) {
        if (s_ramp_gain < target) {
            s_ramp_gain += step;
            if (s_ramp_gain > target) s_ramp_gain = target;
        } else if (s_ramp_gain > target) {
            s_ramp_gain -= step;
            if (s_ramp_gain < target) s_ramp_gain = target;
        }
        long l = lround((double)out[i * 2] * s_ramp_gain);
        long r = lround((double)out[i * 2 + 1] * s_ramp_gain);
        if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
        if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
        out[i * 2] = (int16_t)l;
        out[i * 2 + 1] = (int16_t)r;
    }
}

void tone_set_voice(int voice)
{
    xSemaphoreTake(mtx(), portMAX_DELAY);
    s_config.voice = (voice == TONE_VOICE_PIANO) ? TONE_VOICE_PIANO : TONE_VOICE_SINE;
    xSemaphoreGive(mtx());
}

void tone_set(int freq_hz)
{
    if (freq_hz < TONE_HZ_MIN) freq_hz = TONE_HZ_MIN;
    if (freq_hz > TONE_HZ_MAX) freq_hz = TONE_HZ_MAX;

    xSemaphoreTake(mtx(), portMAX_DELAY);
    s_config.hz = freq_hz;
    /* A fresh note restarts the piano envelope (struck each keypress). */
    if (s_config.voice == TONE_VOICE_PIANO) atomic_store(&s_retrigger, true);
    s_config.on = true;
    xSemaphoreGive(mtx());
}

void tone_set_amplitude(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    xSemaphoreTake(mtx(), portMAX_DELAY);
    s_config.amp_pct = pct;
    xSemaphoreGive(mtx());
}

void tone_off(void)
{
    xSemaphoreTake(mtx(), portMAX_DELAY);
    s_config.on = false;
    xSemaphoreGive(mtx());
}

void tone_get(bool *enabled, int *freq_hz)
{
    tone_config_t cfg = snapshot_config();
    if (enabled) *enabled = cfg.on;
    if (freq_hz) *freq_hz = cfg.hz;
}

#ifdef UNIT_TEST
/* Test-only: snap the ramp to the current config's target instantly, so
 * tests can assert steady-state on/off behavior without waiting out the
 * full TONE_RAMP_SAMPLES transition. The ramp itself is covered by its own
 * dedicated test. */
void tone_test_snap_gain(void)
{
    tone_config_t cfg = snapshot_config();
    s_ramp_gain = cfg.on ? (float)cfg.amp_pct / 100.0f : 0.0f;
}
#endif
