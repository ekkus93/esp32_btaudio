/**
 * test_uart_upsample2x.c — unit tests for the 2x stereo linear upsampler
 *
 * audio_upsample2x_s16_stereo() doubles 22050 Hz stereo s16 to 44100 Hz:
 * for each input frame (L,R) it emits [( (prev+cur)/2 ), (cur)] per
 * channel, carrying prev across calls so split feeds are seamless.
 *
 * Covers:
 *   - DC in -> DC out
 *   - exact interpolation values (including first-frame use of prev[])
 *   - continuity across split calls (one call == two calls)
 *   - channel independence (L and R interpolate separately)
 *   - negative-value rounding pinned to C truncation
 *   - zero-frame call is a no-op
 */

#include <string.h>
#include "unity.h"
#include "uart_source.h"

void setUp(void) {}
void tearDown(void) {}

void test_dc_input_gives_dc_output(void)
{
    int16_t prev[2] = { 1000, -1000 };
    int16_t in[8];  /* 4 frames of DC */
    int16_t out[16];
    for (int i = 0; i < 4; i++) {
        in[2 * i] = 1000;
        in[2 * i + 1] = -1000;
    }

    audio_upsample2x_s16_stereo(in, 4, out, prev);

    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_INT16(1000, out[2 * i]);
        TEST_ASSERT_EQUAL_INT16(-1000, out[2 * i + 1]);
    }
    TEST_ASSERT_EQUAL_INT16(1000, prev[0]);
    TEST_ASSERT_EQUAL_INT16(-1000, prev[1]);
}

void test_exact_interpolation_values(void)
{
    int16_t prev[2] = { 0, 0 };
    const int16_t in[4] = { 100, -100, 200, -200 }; /* 2 frames */
    int16_t out[8];

    audio_upsample2x_s16_stereo(in, 2, out, prev);

    const int16_t expected[8] = {
        50, -50,    /* midpoint prev(0) -> 100 */
        100, -100,  /* frame 1 */
        150, -150,  /* midpoint 100 -> 200 */
        200, -200,  /* frame 2 */
    };
    TEST_ASSERT_EQUAL_INT16_ARRAY(expected, out, 8);
    TEST_ASSERT_EQUAL_INT16(200, prev[0]);
    TEST_ASSERT_EQUAL_INT16(-200, prev[1]);
}

void test_continuity_across_split_calls(void)
{
    const int16_t in[8] = { 100, 10, 200, 20, 300, 30, 400, 40 }; /* 4 frames */

    int16_t prev_a[2] = { 0, 0 };
    int16_t out_a[16];
    audio_upsample2x_s16_stereo(in, 4, out_a, prev_a);

    int16_t prev_b[2] = { 0, 0 };
    int16_t out_b[16];
    audio_upsample2x_s16_stereo(in, 1, out_b, prev_b);
    audio_upsample2x_s16_stereo(in + 2, 3, out_b + 4, prev_b);

    TEST_ASSERT_EQUAL_INT16_ARRAY(out_a, out_b, 16);
    TEST_ASSERT_EQUAL_INT16(prev_a[0], prev_b[0]);
    TEST_ASSERT_EQUAL_INT16(prev_a[1], prev_b[1]);
}

void test_channel_independence(void)
{
    int16_t prev[2] = { 0, 5000 };
    const int16_t in[4] = { 1000, 5000, 2000, 5000 }; /* L ramps, R constant */
    int16_t out[8];

    audio_upsample2x_s16_stereo(in, 2, out, prev);

    /* L: 0->1000->2000 interpolated */
    TEST_ASSERT_EQUAL_INT16(500, out[0]);
    TEST_ASSERT_EQUAL_INT16(1000, out[2]);
    TEST_ASSERT_EQUAL_INT16(1500, out[4]);
    TEST_ASSERT_EQUAL_INT16(2000, out[6]);
    /* R: flat 5000 throughout */
    TEST_ASSERT_EQUAL_INT16(5000, out[1]);
    TEST_ASSERT_EQUAL_INT16(5000, out[3]);
    TEST_ASSERT_EQUAL_INT16(5000, out[5]);
    TEST_ASSERT_EQUAL_INT16(5000, out[7]);
}

void test_negative_rounding_truncates_toward_zero(void)
{
    int16_t prev[2] = { -1, 1 };
    const int16_t in[2] = { -2, 2 }; /* midpoints: -3/2 and 3/2 */
    int16_t out[4];

    audio_upsample2x_s16_stereo(in, 1, out, prev);

    TEST_ASSERT_EQUAL_INT16(-1, out[0]); /* C truncation: -3/2 == -1 */
    TEST_ASSERT_EQUAL_INT16(1, out[1]);  /*                3/2 ==  1 */
    TEST_ASSERT_EQUAL_INT16(-2, out[2]);
    TEST_ASSERT_EQUAL_INT16(2, out[3]);
}

void test_extreme_values_no_overflow(void)
{
    int16_t prev[2] = { 32767, -32768 };
    const int16_t in[2] = { 32767, -32768 };
    int16_t out[4];

    audio_upsample2x_s16_stereo(in, 1, out, prev);

    TEST_ASSERT_EQUAL_INT16(32767, out[0]);
    TEST_ASSERT_EQUAL_INT16(-32768, out[1]);
    TEST_ASSERT_EQUAL_INT16(32767, out[2]);
    TEST_ASSERT_EQUAL_INT16(-32768, out[3]);
}

void test_zero_frames_is_noop(void)
{
    int16_t prev[2] = { 123, -456 };
    int16_t out[2] = { 0x7EAD, 0x0BEE };
    const int16_t in[2] = { 1, 2 };

    audio_upsample2x_s16_stereo(in, 0, out, prev);

    TEST_ASSERT_EQUAL_INT16(123, prev[0]);
    TEST_ASSERT_EQUAL_INT16(-456, prev[1]);
    TEST_ASSERT_EQUAL_INT16(0x7EAD, out[0]); /* untouched */
    TEST_ASSERT_EQUAL_INT16(0x0BEE, out[1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dc_input_gives_dc_output);
    RUN_TEST(test_exact_interpolation_values);
    RUN_TEST(test_continuity_across_split_calls);
    RUN_TEST(test_channel_independence);
    RUN_TEST(test_negative_rounding_truncates_toward_zero);
    RUN_TEST(test_extreme_values_no_overflow);
    RUN_TEST(test_zero_frames_is_noop);
    return UNITY_END();
}
