/* RADIO-2b: host tests for the pure resampler. */
#include "unity.h"
#include "radio_resampler.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

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
    /* 22.05k -> 44.1k = 2x upsampling. The streaming algorithm never
     * fabricates a sample beyond the last real input frame it can
     * interpolate against, so the exact end-of-stream count can be one
     * frame short of the naive 2x — the TODO 6.3 long-run test explicitly
     * allows this ("bounded by one frame depending on end-of-stream
     * policy"). */
    TEST_ASSERT_INT_WITHIN(1, (int)(in_frames * 2), (int)total_out);
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

/* TODO 6.3: exact reference ramp — pins down the previously-wrong math
 * (RESAMPLE-001). A 48k->44.1k linear resampler of 0,1000,...,5000 must
 * land near these exact source positions. */
void test_48k_ramp_matches_reference(void)
{
    const int16_t in[] = {
        0, 0, 1000, 1000, 2000, 2000, 3000, 3000, 4000, 4000, 5000, 5000,
    };
    const int16_t expected[] = { 0, 1088, 2177, 3265, 4354 };

    radio_resampler_t r;
    TEST_ASSERT_TRUE(radio_resampler_init(&r, 48000, 2));

    int16_t out[32] = {0};
    size_t used = 0;
    size_t count = radio_resampler_run(&r, in, 6, out, 16, &used);

    TEST_ASSERT_EQUAL_UINT(6, used);
    TEST_ASSERT_EQUAL_UINT(5, count);
    for (size_t i = 0; i < count; i++) {
        TEST_ASSERT_INT16_WITHIN(1, expected[i], out[i * 2]);
        TEST_ASSERT_INT16_WITHIN(1, expected[i], out[i * 2 + 1]);
    }
}

/* TODO 6.3: chunk-boundary equivalence — one call, one-frame-at-a-time, and
 * arbitrary chunk splits must all produce bit-identical output for the same
 * input stream. This is the property the old implementation's caller-side
 * looping relied on without ever being tested directly. */
static size_t run_resampler_in_chunks(int src_rate, const int16_t *in, size_t in_frames,
                                      const size_t *chunk_sizes, size_t n_chunks,
                                      int16_t *out, size_t out_cap)
{
    radio_resampler_t r;
    TEST_ASSERT_TRUE(radio_resampler_init(&r, src_rate, 2));

    size_t in_off = 0, out_off = 0, chunk_i = 0;
    while (in_off < in_frames) {
        size_t want = (n_chunks == 0) ? (in_frames - in_off) : chunk_sizes[chunk_i % n_chunks];
        if (want > in_frames - in_off) want = in_frames - in_off;
        if (want == 0) want = 1;
        chunk_i++;

        size_t remaining_in = want;
        const int16_t *chunk_ptr = in + 2 * in_off;
        while (remaining_in > 0) {
            size_t used = 0;
            TEST_ASSERT_TRUE(out_off < out_cap);
            size_t o = radio_resampler_run(&r, chunk_ptr, remaining_in,
                                           out + 2 * out_off, out_cap - out_off, &used);
            out_off += o;
            chunk_ptr += 2 * used;
            remaining_in -= used;
            if (used == 0 && o == 0) break;  /* out_cap exhausted */
        }
        in_off += want;
    }
    return out_off;
}

void test_chunk_boundary_equivalence(void)
{
    const size_t in_frames = 200;
    int16_t in[400];
    for (size_t i = 0; i < in_frames; i++) {
        in[2 * i] = (int16_t)((i * 137) % 4000 - 2000);
        in[2 * i + 1] = (int16_t)((i * 251) % 4000 - 2000);
    }

    int16_t out_one_call[512] = {0};
    size_t n_one = run_resampler_in_chunks(48000, in, in_frames, NULL, 0, out_one_call, 512);

    int16_t out_one_frame[512] = {0};
    const size_t chunk1[] = {1};
    size_t n_frame = run_resampler_in_chunks(48000, in, in_frames, chunk1, 1, out_one_frame, 512);

    int16_t out_random[512] = {0};
    const size_t chunk_random[] = {2, 7, 1, 13, 5, 3, 11};
    size_t n_random = run_resampler_in_chunks(48000, in, in_frames, chunk_random, 7, out_random, 512);

    TEST_ASSERT_EQUAL_UINT(n_one, n_frame);
    TEST_ASSERT_EQUAL_UINT(n_one, n_random);
    TEST_ASSERT_EQUAL_INT16_ARRAY(out_one_call, out_one_frame, n_one * 2);
    TEST_ASSERT_EQUAL_INT16_ARRAY(out_one_call, out_random, n_one * 2);
}

/* TODO 6.3: sine frequency — resample a 1 kHz tone from 48k to 44.1k and
 * verify the output's zero-crossing-derived frequency stays within 1 Hz. */
void test_sine_frequency_preserved(void)
{
    const int src_rate = 48000;
    const double freq = 1000.0;
    const size_t in_frames = (size_t)src_rate;  /* 1 second */
    int16_t *in = (int16_t *)malloc(in_frames * 2 * sizeof(int16_t));
    for (size_t i = 0; i < in_frames; i++) {
        double s = sin(2.0 * M_PI * freq * (double)i / (double)src_rate);
        int16_t v = (int16_t)lround(s * 30000.0);
        in[2 * i] = v;
        in[2 * i + 1] = v;
    }

    radio_resampler_t r;
    TEST_ASSERT_TRUE(radio_resampler_init(&r, src_rate, 2));

    int16_t *out = (int16_t *)malloc((in_frames + 100) * 2 * sizeof(int16_t));
    size_t total_out = 0, offset = 0;
    while (offset < in_frames) {
        size_t used = 0;
        size_t o = radio_resampler_run(&r, in + 2 * offset, in_frames - offset,
                                       out + 2 * total_out, in_frames + 100 - total_out, &used);
        offset += used;
        total_out += o;
        if (o == 0 && used == 0) break;
    }

    /* Count rising zero-crossings on the L channel and derive frequency. */
    int crossings = 0;
    for (size_t i = 1; i < total_out; i++) {
        if (out[2 * (i - 1)] < 0 && out[2 * i] >= 0) crossings++;
    }
    /* Unity's double assertions are excluded by default in this vendored
     * build; compare via TEST_ASSERT_INT_WITHIN on a scaled integer instead. */
    double duration_s = (double)total_out / (double)RESAMPLE_OUT_RATE;
    long measured_hz_x100 = lround((double)crossings / duration_s * 100.0);
    TEST_ASSERT_INT_WITHIN(100, (long)(freq * 100.0), measured_hz_x100);
    free(in);
    free(out);
}

/* TODO 6.3: long-run frame count — 480,000 in @ 48k -> ~441,000 out @ 44.1k,
 * bounded by one frame either way. */
void test_long_run_frame_count(void)
{
    radio_resampler_t r;
    TEST_ASSERT_TRUE(radio_resampler_init(&r, 48000, 2));

    const size_t in_frames = 480000;
    int16_t *in = (int16_t *)calloc(in_frames * 2, sizeof(int16_t));
    for (size_t i = 0; i < in_frames; i++) {
        in[2 * i] = (int16_t)(i % 1000);
        in[2 * i + 1] = (int16_t)(i % 1000);
    }

    int16_t out[8192];
    size_t total_out = 0, total_used = 0, offset = 0;
    while (offset < in_frames) {
        size_t used = 0;
        size_t o = radio_resampler_run(&r, in + 2 * offset, in_frames - offset,
                                       out, sizeof(out) / (2 * sizeof(int16_t)), &used);
        TEST_ASSERT_TRUE(o > 0 || used > 0);
        offset += used;
        total_used += used;
        total_out += o;
    }

    TEST_ASSERT_EQUAL_UINT(in_frames, total_used);
    /* 480000 * 44100/48000 = 441000 exactly; allow a 1-frame slop for
     * end-of-stream rounding. */
    TEST_ASSERT_INT_WITHIN(1, 441000, (int)total_out);
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
    RUN_TEST(test_48k_ramp_matches_reference);
    RUN_TEST(test_chunk_boundary_equivalence);
    RUN_TEST(test_sine_frequency_preserved);
    RUN_TEST(test_long_run_frame_count);
    return UNITY_END();
}
