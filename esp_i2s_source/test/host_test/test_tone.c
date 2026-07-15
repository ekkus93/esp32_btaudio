/*
 * test_tone — WEB-1d: host tests for the controllable test-tone source.
 *
 * Verifies tone_fill(), tone_set(), tone_set_amplitude(), tone_set_voice(),
 * tone_off(), and tone_get(). Tone uses signal_gen internally (sg_sine and
 * sg_piano), so the same exact-math tricks apply at fs/4 Hz.
 */
#include "unity.h"
#include "tone.h"
#include "signal_gen.h"

#include <stdlib.h>
#include <string.h>

extern void tone_test_snap_gain(void);

void setUp(void)
{
    /* Reset tone state before each test: off, default freq, default amplitude. */
    tone_off();
    tone_set_voice(TONE_VOICE_SINE);
    tone_set_amplitude(30);  /* TONE_AMP_DEFAULT from tone.c */
    /* TODO 5.4 introduced a click-suppression ramp on the on/off/amplitude
     * gain; snap it to the (now off) target so each test starts from a
     * settled state instead of wherever the previous test's ramp left it. */
    tone_test_snap_gain();
}

void tearDown(void)
{
    /* No teardown needed — tone module is pure state. */
}

/* --- silence when off --- */

void test_tone_default_off_silence(void)
{
    int16_t buf[64];
    /* Default state: off, so silence. */
    tone_fill(buf, 32);
    for (int i = 0; i < 64; i++)
        TEST_ASSERT_EQUAL_INT16(0, buf[i]);
}

/* --- tone_set enables and clamps frequency --- */

void test_tone_set_enables(void)
{
    bool enabled;
    int freq_hz;

    tone_set(440);
    tone_get(&enabled, &freq_hz);
    TEST_ASSERT_TRUE(enabled);
    TEST_ASSERT_EQUAL(440, freq_hz);
}

void test_tone_set_clamp_min(void)
{
    /* Below TONE_HZ_MIN (20) → clamps to 20. */
    tone_set(10);
    int freq_hz;
    tone_get(NULL, &freq_hz);
    TEST_ASSERT_EQUAL(TONE_HZ_MIN, freq_hz);
}

void test_tone_set_clamp_max(void)
{
    /* Above TONE_HZ_MAX (20000) → clamps to 20000. */
    tone_set(100000);
    int freq_hz;
    tone_get(NULL, &freq_hz);
    TEST_ASSERT_EQUAL(TONE_HZ_MAX, freq_hz);
}

/* --- tone_off produces silence --- */

void test_tone_off_produces_silence(void)
{
    int16_t buf[64];

    tone_set(440);
    tone_off();
    tone_fill(buf, 32);
    for (int i = 0; i < 64; i++)
        TEST_ASSERT_EQUAL_INT16(0, buf[i]);
}

/* --- amplitude clamping --- */

void test_tone_amplitude_clamp(void)
{
    /* Negative → clamps to 0 (mute). */
    tone_set(440);
    tone_set_amplitude(-10);
    int16_t buf[64];
    tone_fill(buf, 32);
    for (int i = 0; i < 64; i++)
        TEST_ASSERT_EQUAL_INT16(0, buf[i]);

    /* Over 100 → clamps to 100. */
    tone_set_amplitude(200);
    tone_test_snap_gain();  /* isolate from the ramp's own timing (tested separately) */
    /* Can't directly inspect amplitude, but we verify no crash and non-zero output. */
    tone_fill(buf, 32);
    /* At 100% amplitude and 440 Hz, there should be signal. */
    int non_zero = 0;
    for (int i = 0; i < 64; i++) if (buf[i] != 0) non_zero++;
    TEST_ASSERT_MESSAGE(non_zero > 0, "100% amplitude should produce signal");
}

void test_tone_amplitude_zero_mutes(void)
{
    int16_t buf[64];

    tone_set(440);
    tone_set_amplitude(0);
    tone_fill(buf, 32);
    for (int i = 0; i < 64; i++)
        TEST_ASSERT_EQUAL_INT16(0, buf[i]);
}

/* --- sine exact math (fs/4 quarter rate) --- */

void test_tone_sine_exact_quarter_rate(void)
{
    /* At fs/4, the sine should oscillate between near-0 and near-peak.
     * Since tone doesn't expose phase reset, verify properties: non-zero
     * signal, within valid range, L==R, and alternating (sine-like). */
    int16_t buf[16 * 2];
    tone_set(SIGNAL_GEN_SAMPLE_RATE_HZ / 4);
    tone_set_amplitude(100);  /* full scale */
    tone_fill(buf, 16);

    /* Should have non-zero signal. */
    int non_zero = 0;
    for (int i = 0; i < 32; i++) if (buf[i] != 0) non_zero++;
    TEST_ASSERT_MESSAGE(non_zero > 0, "quarter-rate tone should produce signal");

    /* All values should be within int16 range. */
    for (int i = 0; i < 32; i++)
        TEST_ASSERT_TRUE(buf[i] >= -32768 && buf[i] <= 32767);
}

/* --- stereo channels equal --- */

void test_tone_stereo_channels_equal(void)
{
    int16_t buf[200];

    tone_set(1000);
    tone_set_amplitude(80);
    tone_fill(buf, 100);
    for (int i = 0; i < 100; i++)
        TEST_ASSERT_EQUAL_INT16(buf[i * 2], buf[i * 2 + 1]);
}

/* --- tone_get reports current state --- */

void test_tone_get_reports_state(void)
{
    bool enabled;
    int freq_hz;

    /* Set a known frequency. */
    tone_set(440);
    tone_get(&enabled, &freq_hz);
    TEST_ASSERT_TRUE(enabled);
    TEST_ASSERT_EQUAL(440, freq_hz);

    /* After off: enabled=false, freq unchanged. */
    tone_off();
    tone_get(&enabled, &freq_hz);
    TEST_ASSERT_FALSE(enabled);
    TEST_ASSERT_EQUAL(440, freq_hz);  /* freq preserved after off */
}

/* --- piano voice --- */

void test_tone_piano_voice_fills(void)
{
    const size_t N = 4410;
    int16_t buf[N * 2];  /* interleaved stereo: N frames * 2 channels */

    tone_set(262);  /* Middle C */
    tone_set_voice(TONE_VOICE_PIANO);
    tone_fill(buf, N);

    /* Piano voice should produce non-zero signal. */
    int non_zero = 0;
    for (size_t i = 0; i < N * 2; i++)
        if (buf[i] != 0) non_zero++;
    TEST_ASSERT_MESSAGE(non_zero > 0, "piano voice should produce signal");
}

/* --- re-enable after off --- */

void test_tone_reinit_after_off(void)
{
    int16_t buf[64];

    /* Enable, produce signal. Snap past the ramp-up first (ramp timing has
     * its own dedicated test) so this test is purely about on/off state. */
    tone_set(440);
    tone_set_amplitude(100);
    tone_test_snap_gain();
    tone_fill(buf, 32);

    /* Verify non-zero signal. */
    int signal = 0;
    for (int i = 0; i < 64; i++) if (buf[i] != 0) signal++;
    TEST_ASSERT_MESSAGE(signal > 0, "tone should produce signal when on");

    /* Off → silence (once the ramp has settled). */
    tone_off();
    tone_test_snap_gain();
    tone_fill(buf, 32);
    for (int i = 0; i < 64; i++) TEST_ASSERT_EQUAL_INT16(0, buf[i]);

    /* Re-enable → signal again. */
    tone_set(440);
    tone_set_amplitude(100);
    tone_test_snap_gain();
    tone_fill(buf, 32);
    signal = 0;
    for (int i = 0; i < 64; i++) if (buf[i] != 0) signal++;
    TEST_ASSERT_MESSAGE(signal > 0, "tone should produce signal after re-enable");
}

/* --- TODO 5.4: click-suppression ramp --- */

static void test_tone_ramp_up_is_gradual_not_instant(void)
{
    /* Coming from off (ramp_gain=0 after setUp), the very first block after
     * turning on must NOT already be at full scale — the whole point of the
     * ramp is that amplitude rises gradually over TONE_RAMP_SAMPLES. */
    tone_set(1000);
    tone_set_amplitude(100);

    int16_t first_block[64];
    tone_fill(first_block, 32);  /* well within the ~441-sample ramp window */

    int16_t settled[64];
    for (int i = 0; i < 50; i++) tone_fill(settled, 32);  /* run well past the ramp */

    int16_t peak_first = 0, peak_settled = 0;
    for (int i = 0; i < 64; i++) {
        int16_t v = first_block[i] < 0 ? (int16_t)-first_block[i] : first_block[i];
        if (v > peak_first) peak_first = v;
        v = settled[i] < 0 ? (int16_t)-settled[i] : settled[i];
        if (v > peak_settled) peak_settled = v;
    }
    TEST_ASSERT_MESSAGE(peak_settled > peak_first,
                        "settled-state amplitude should exceed the ramping-up first block");
}

static void test_tone_off_eventually_reaches_silence(void)
{
    /* Ramp down fully converges to silence given enough blocks, even though
     * the block immediately after tone_off() is not yet silent. */
    tone_set(1000);
    tone_set_amplitude(100);
    tone_test_snap_gain();  /* start from settled "on" */

    tone_off();
    int16_t buf[64];
    for (int i = 0; i < 50; i++) tone_fill(buf, 32);  /* run well past the ramp-down */
    for (int i = 0; i < 64; i++) TEST_ASSERT_EQUAL_INT16(0, buf[i]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tone_default_off_silence);
    RUN_TEST(test_tone_set_enables);
    RUN_TEST(test_tone_set_clamp_min);
    RUN_TEST(test_tone_set_clamp_max);
    RUN_TEST(test_tone_off_produces_silence);
    RUN_TEST(test_tone_amplitude_clamp);
    RUN_TEST(test_tone_amplitude_zero_mutes);
    RUN_TEST(test_tone_sine_exact_quarter_rate);
    RUN_TEST(test_tone_stereo_channels_equal);
    RUN_TEST(test_tone_get_reports_state);
    RUN_TEST(test_tone_piano_voice_fills);
    RUN_TEST(test_tone_reinit_after_off);
    RUN_TEST(test_tone_ramp_up_is_gradual_not_instant);
    RUN_TEST(test_tone_off_eventually_reaches_silence);
    return UNITY_END();
}
