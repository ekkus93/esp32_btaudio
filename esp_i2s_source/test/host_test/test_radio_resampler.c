/* RADIO-2b: host tests for the pure resampler. */
#include "unity.h"
#include "radio_resampler.h"
#include <string.h>
#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

void test_passthrough_44100_stereo(void)
{
    radio_resampler_t r;
    radio_resampler_init(&r, 44100, 2);
    int16_t in[8] = {1, 2, 3, 4, 5, 6, 7, 8};  /* 4 stereo frames */
    int16_t out[8] = {0};
    size_t used = 0;
    size_t o = radio_resampler_run(&r, in, 4, out, 4, &used);
    TEST_ASSERT_EQUAL_UINT(4, o);
    TEST_ASSERT_EQUAL_UINT(4, used);
    TEST_ASSERT_EQUAL_INT16_ARRAY(in, out, 8);
}

void test_dc_in_dc_out_upsample(void)
{
    /* Constant input must stay constant through interpolation (22.05k->44.1k). */
    radio_resampler_t r;
    radio_resampler_init(&r, 22050, 2);
    int16_t in[64];
    for (int i = 0; i < 32; i++) { in[2 * i] = 1000; in[2 * i + 1] = -500; }
    int16_t out[256] = {0};
    size_t used = 0;
    size_t o = radio_resampler_run(&r, in, 32, out, 128, &used);
    TEST_ASSERT_TRUE(o > 32);   /* upsampling -> more frames out than in */
    for (size_t k = 0; k < o; k++) {
        TEST_ASSERT_INT16_WITHIN(1, 1000, out[2 * k]);
        TEST_ASSERT_INT16_WITHIN(1, -500, out[2 * k + 1]);
    }
}

void test_mono_upmixed_to_stereo(void)
{
    radio_resampler_t r;
    radio_resampler_init(&r, 44100, 1);   /* mono in */
    int16_t in[8] = {100, 200, 300, 400, 500, 600, 700, 800};
    int16_t out[32] = {0};
    size_t used = 0;
    size_t o = radio_resampler_run(&r, in, 8, out, 16, &used);
    TEST_ASSERT_TRUE(o > 0);
    for (size_t k = 0; k < o; k++) {
        TEST_ASSERT_EQUAL_INT16(out[2 * k], out[2 * k + 1]);  /* L == R */
    }
}

void test_downsample_produces_fewer_frames(void)
{
    /* 48k -> 44.1k: ~0.919 output frames per input frame. */
    radio_resampler_t r;
    radio_resampler_init(&r, 48000, 2);
    int16_t *in = calloc(480 * 2, sizeof(int16_t));
    for (int i = 0; i < 480; i++) { in[2 * i] = 2000; in[2 * i + 1] = 2000; }
    int16_t out[1024] = {0};
    size_t used = 0;
    size_t o = radio_resampler_run(&r, in, 480, out, 512, &used);
    /* ~441 out for 480 in */
    TEST_ASSERT_INT_WITHIN(3, 441, (int)o);
    TEST_ASSERT_INT16_WITHIN(1, 2000, out[0]);
    free(in);
}

void test_streaming_continuity_dc(void)
{
    /* Splitting a constant stream across two calls still yields constant out. */
    radio_resampler_t r;
    radio_resampler_init(&r, 32000, 2);
    int16_t in[128];
    for (int i = 0; i < 64; i++) { in[2 * i] = 777; in[2 * i + 1] = 777; }
    int16_t out[256] = {0};
    size_t used = 0, total = 0;
    total += radio_resampler_run(&r, in, 32, out + 2 * total, 200, &used);
    total += radio_resampler_run(&r, in + 64, 32, out + 2 * total, 200 - total, &used);
    TEST_ASSERT_TRUE(total > 0);
    for (size_t k = 0; k < total; k++) {
        TEST_ASSERT_INT16_WITHIN(1, 777, out[2 * k]);
    }
}

void test_output_bounded_by_cap(void)
{
    radio_resampler_t r;
    radio_resampler_init(&r, 22050, 2);   /* upsampling: lots of output */
    int16_t in[200] = {0};
    int16_t out[8] = {0};
    size_t used = 0;
    size_t o = radio_resampler_run(&r, in, 100, out, 4, &used);
    TEST_ASSERT_EQUAL_UINT(4, o);          /* never exceeds out_cap */
    TEST_ASSERT_TRUE(used <= 100);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_passthrough_44100_stereo);
    RUN_TEST(test_dc_in_dc_out_upsample);
    RUN_TEST(test_mono_upmixed_to_stereo);
    RUN_TEST(test_downsample_produces_fewer_frames);
    RUN_TEST(test_streaming_continuity_dc);
    RUN_TEST(test_output_bounded_by_cap);
    return UNITY_END();
}
