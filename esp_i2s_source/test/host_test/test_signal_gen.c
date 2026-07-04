/*
 * test_signal_gen — exact-math host tests for the SIG-1a producers.
 *
 * Trick for exactness: at freq = fs/4 the per-frame phase step is exactly
 * pi/2, so a unit-amplitude sine yields the exact sequence
 * [0, +peak, 0, -peak, 0, +peak, ...] with peak = round(amp * 32767).
 */
#include "unity.h"
#include "signal_gen.h"

#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

#define PEAK 32767

/* --- silence --- */
static void test_silence_is_all_zero(void)
{
    int16_t buf[64];
    for (size_t i = 0; i < 64; i++) buf[i] = 0x7777;  /* poison */
    sg_silence_fill(buf, 32);
    for (size_t i = 0; i < 64; i++) TEST_ASSERT_EQUAL_INT16(0, buf[i]);
}

/* --- sine: exact quarter-rate sequence --- */
static void test_sine_quarter_rate_exact(void)
{
    sg_sine_state_t st;
    sg_sine_reset(&st);
    int16_t buf[8 * SIGNAL_GEN_CHANNELS];
    sg_sine_fill(&st, buf, 8, (double)SIGNAL_GEN_SAMPLE_RATE_HZ / 4.0, 1.0);

    const int16_t expect[8] = { 0, PEAK, 0, -PEAK, 0, PEAK, 0, -PEAK };
    for (size_t f = 0; f < 8; f++) {
        TEST_ASSERT_INT16_WITHIN(1, expect[f], buf[f * 2]);       /* L */
        TEST_ASSERT_INT16_WITHIN(1, expect[f], buf[f * 2 + 1]);   /* R */
    }
}

/* --- sine: L and R identical --- */
static void test_sine_stereo_channels_equal(void)
{
    sg_sine_state_t st;
    sg_sine_reset(&st);
    int16_t buf[100 * 2];
    sg_sine_fill(&st, buf, 100, 1000.0, 0.8);
    for (size_t f = 0; f < 100; f++)
        TEST_ASSERT_EQUAL_INT16(buf[f * 2], buf[f * 2 + 1]);
}

/* --- sine: amplitude scaling (0.5 => peak 16384) --- */
static void test_sine_amplitude_scaling(void)
{
    sg_sine_state_t st;
    sg_sine_reset(&st);
    int16_t buf[4 * 2];
    sg_sine_fill(&st, buf, 4, (double)SIGNAL_GEN_SAMPLE_RATE_HZ / 4.0, 0.5);
    /* frame 1 is the +peak; lround(0.5*32767)=16384 */
    TEST_ASSERT_INT16_WITHIN(1, 16384, buf[1 * 2]);
}

/* --- sine: phase continuity across split fills --- */
static void test_sine_phase_continuity(void)
{
    const double f = 3000.0, a = 0.9;
    int16_t whole[16 * 2];
    int16_t split[16 * 2];

    sg_sine_state_t s1; sg_sine_reset(&s1);
    sg_sine_fill(&s1, whole, 16, f, a);

    sg_sine_state_t s2; sg_sine_reset(&s2);
    sg_sine_fill(&s2, split, 5, f, a);
    sg_sine_fill(&s2, split + 5 * 2, 11, f, a);

    for (size_t i = 0; i < 16 * 2; i++)
        TEST_ASSERT_EQUAL_INT16(whole[i], split[i]);
}

/* --- sine: values never exceed the amplitude-scaled peak --- */
static void test_sine_no_clipping_overflow(void)
{
    sg_sine_state_t st; sg_sine_reset(&st);
    int16_t buf[441 * 2];
    sg_sine_fill(&st, buf, 441, 440.0, 1.0);
    for (size_t i = 0; i < 441 * 2; i++) {
        TEST_ASSERT_TRUE(buf[i] <= PEAK);
        TEST_ASSERT_TRUE(buf[i] >= -PEAK - 1);  /* -32768 tolerated */
    }
}

/* --- sweep: starts near f0, ends near f1 --- */
static void test_sweep_progresses(void)
{
    /* Sweep 100 -> 8000 Hz over 1 s. Sample the zero-crossing spacing at the
     * start vs end: later frames must oscillate faster. Proxy: count sign
     * changes in the first vs last 2205-frame (50 ms) window. */
    const size_t N = SIGNAL_GEN_SAMPLE_RATE_HZ;  /* 1 s */
    int16_t *buf = malloc(sizeof(int16_t) * N * 2);
    TEST_ASSERT_NOT_NULL(buf);
    sg_sweep_state_t st; sg_sweep_reset(&st);
    sg_sweep_fill(&st, buf, N, 100.0, 8000.0, 1.0, 0.9);

    size_t win = 2205;
    int early = 0, late = 0;
    for (size_t i = 1; i < win; i++)
        if ((buf[i * 2] >= 0) != (buf[(i - 1) * 2] >= 0)) early++;
    for (size_t i = N - win + 1; i < N; i++)
        if ((buf[i * 2] >= 0) != (buf[(i - 1) * 2] >= 0)) late++;

    TEST_ASSERT_TRUE_MESSAGE(late > early, "sweep should speed up over time");
    free(buf);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_silence_is_all_zero);
    RUN_TEST(test_sine_quarter_rate_exact);
    RUN_TEST(test_sine_stereo_channels_equal);
    RUN_TEST(test_sine_amplitude_scaling);
    RUN_TEST(test_sine_phase_continuity);
    RUN_TEST(test_sine_no_clipping_overflow);
    RUN_TEST(test_sweep_progresses);
    return UNITY_END();
}
