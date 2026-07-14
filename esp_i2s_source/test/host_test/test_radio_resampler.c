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

/* RH-S3-04: all input frames must be consumed by looping the resampler call.
 * These tests verify the looping pattern consumes all input across rates. */

void test_resampler_consumes_all_22050(void)
{
    /* 22.05 kHz stereo -> 44.1 kHz: 2x upsampling.
     * 4096 input frames -> 8192 output frames.
     * Output buffer smaller than total output forces multiple calls. */
    radio_resampler_t r;
    radio_resampler_init(&r, 22050, 2);

    const size_t in_frames = 4096;
    int16_t *in = (int16_t *)calloc(in_frames * 2, sizeof(int16_t));
    for (size_t i = 0; i < in_frames; i++) {
        in[2 * i] = (int16_t)(i % 1000);
        in[2 * i + 1] = (int16_t)(i % 1000);
    }

    int16_t out[4096];  /* forces multiple resampler calls */
    size_t total_used = 0, offset = 0;
    while (offset < in_frames) {
        size_t used = 0;
        size_t o = radio_resampler_run(&r, in + 2 * offset, in_frames - offset,
                                       out, sizeof(out) / (2 * sizeof(int16_t)), &used);
        TEST_ASSERT_TRUE(o > 0);
        offset += used;
        total_used += used;
    }
    TEST_ASSERT_EQUAL_UINT(in_frames, total_used);  /* all consumed */
    free(in);
}

void test_resampler_consumes_all_32k(void)
{
    radio_resampler_t r;
    radio_resampler_init(&r, 32000, 2);

    const size_t in_frames = 2048;
    int16_t *in = (int16_t *)calloc(in_frames * 2, sizeof(int16_t));
    for (size_t i = 0; i < in_frames; i++) {
        in[2 * i] = (int16_t)(i % 500);
        in[2 * i + 1] = (int16_t)(i % 500);
    }

    int16_t out[4096];
    size_t total_used = 0, offset = 0;
    while (offset < in_frames) {
        size_t used = 0;
        size_t o = radio_resampler_run(&r, in + 2 * offset, in_frames - offset,
                                       out, sizeof(out) / (2 * sizeof(int16_t)), &used);
        TEST_ASSERT_TRUE(o > 0);
        offset += used;
        total_used += used;
    }
    TEST_ASSERT_EQUAL_UINT(in_frames, total_used);
    free(in);
}

void test_resampler_consumes_all_48k(void)
{
    /* 48 kHz -> 44.1 kHz downsample. */
    radio_resampler_t r;
    radio_resampler_init(&r, 48000, 2);

    const size_t in_frames = 4800;
    int16_t *in = (int16_t *)calloc(in_frames * 2, sizeof(int16_t));
    for (size_t i = 0; i < in_frames; i++) {
        in[2 * i] = (int16_t)(i % 3000);
        in[2 * i + 1] = (int16_t)(i % 3000);
    }

    int16_t out[4096];
    size_t total_used = 0, offset = 0;
    while (offset < in_frames) {
        size_t used = 0;
        size_t o = radio_resampler_run(&r, in + 2 * offset, in_frames - offset,
                                       out, sizeof(out) / (2 * sizeof(int16_t)), &used);
        TEST_ASSERT_TRUE(o > 0);
        offset += used;
        total_used += used;
    }
    TEST_ASSERT_EQUAL_UINT(in_frames, total_used);
    free(in);
}

void test_resampler_small_output_no_data_loss(void)
{
    /* Tiny output capacity forces many resampler calls.
     * Verify no data loss (all input consumed, output count correct). */
    radio_resampler_t r;
    radio_resampler_init(&r, 22050, 2);

    const size_t in_frames = 64;
    int16_t in[128];
    for (size_t i = 0; i < in_frames; i++) {
        in[2 * i] = (int16_t)i;
        in[2 * i + 1] = (int16_t)(i * 2);
    }

    int16_t out[8];  /* tiny buffer */
    size_t total_used = 0, total_out = 0, offset = 0;
    while (offset < in_frames) {
        size_t used = 0;
        size_t o = radio_resampler_run(&r, in + 2 * offset, in_frames - offset,
                                       out, sizeof(out) / (2 * sizeof(int16_t)), &used);
        TEST_ASSERT_TRUE(o > 0);
        TEST_ASSERT_TRUE(used > 0);
        offset += used;
        total_used += used;
        total_out += o;
    }
    TEST_ASSERT_EQUAL_UINT(in_frames, total_used);
    /* 22.05k -> 44.1k = 2x upsampling */
    TEST_ASSERT_EQUAL_UINT(in_frames * 2, total_out);
}

void test_resampler_mono_all_consumed(void)
{
    /* Mono input: offset arithmetic uses 1 sample/frame (not 2). */
    radio_resampler_t r;
    radio_resampler_init(&r, 44100, 1);  /* mono */

    const size_t in_frames = 1024;
    int16_t *in = (int16_t *)calloc(in_frames, sizeof(int16_t));
    for (size_t i = 0; i < in_frames; i++) {
        in[i] = (int16_t)(i % 1000);
    }

    int16_t out[4096];  /* stereo output */
    size_t total_used = 0, offset = 0;
    while (offset < in_frames) {
        size_t used = 0;
        size_t o = radio_resampler_run(&r, in + offset, in_frames - offset,
                                       out, sizeof(out) / (2 * sizeof(int16_t)), &used);
        TEST_ASSERT_TRUE(o > 0);
        offset += used;
        total_used += used;
    }
    TEST_ASSERT_EQUAL_UINT(in_frames, total_used);
    free(in);
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
    RUN_TEST(test_resampler_consumes_all_22050);
    RUN_TEST(test_resampler_consumes_all_32k);
    RUN_TEST(test_resampler_consumes_all_48k);
    RUN_TEST(test_resampler_small_output_no_data_loss);
    RUN_TEST(test_resampler_mono_all_consumed);
    return UNITY_END();
}
