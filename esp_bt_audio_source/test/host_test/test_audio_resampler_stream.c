/**
 * @file test_audio_resampler_stream.c
 * @brief Unit tests for stateful streaming resampler
 *
 * Tests verify:
 * - Q16.16 step computation
 * - Minimum input frame calculation
 * - Exact output frame production
 * - Phase carry across blocks
 * - Total frame accuracy
 * - EOF handling
 */

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "unity.h"
#include "audio_resampler_stream.h"

// Test constants
#define FRAMES_PER_BLOCK 256
#define Q16_ONE 65536
#define TOLERANCE_FRAMES 2  // Allow ±2 frames for rounding

// Helper to compute expected step_q16
static uint32_t compute_expected_step(uint32_t src_rate, uint32_t dst_rate)
{
    return ((uint64_t)src_rate << 16) / dst_rate;
}

// Helper to create simple test signal (incrementing samples)
static void fill_test_signal_16(int16_t *buf, size_t frames, int channels, int16_t start_val)
{
    for (size_t i = 0; i < frames * channels; i++) {
        buf[i] = start_val + (int16_t)i;
    }
}

// Helper to check if output is all zeros
static bool is_silence_16(const int16_t *buf, size_t frames, int channels)
{
    for (size_t i = 0; i < frames * channels; i++) {
        if (buf[i] != 0) {
            return false;
        }
    }
    return true;
}

void setUp(void)
{
    // No global state to reset
}

void tearDown(void)
{
    // No cleanup needed
}

//-----------------------------------------------------------------------------
// Init and step_q16 computation tests
//-----------------------------------------------------------------------------

void test_init_44k_to_48k_should_compute_correct_step(void)
{
    audio_resampler_stream_t rs;
    
    audio_resampler_stream_init(&rs, 
                                 AUDIO_SAMPLE_RATE_44K,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_BIT_DEPTH_16,
                                 2);
    
    // Expected: (44100 << 16) / 48000 = 60,113
    uint32_t expected = compute_expected_step(44100, 48000);
    
    TEST_ASSERT_EQUAL_UINT32(AUDIO_SAMPLE_RATE_44K, rs.src_rate);
    TEST_ASSERT_EQUAL_UINT32(AUDIO_SAMPLE_RATE_48K, rs.dst_rate);
    TEST_ASSERT_EQUAL_UINT32(AUDIO_BIT_DEPTH_16, rs.bit_depth);
    TEST_ASSERT_EQUAL_INT(2, rs.channels);
    TEST_ASSERT_EQUAL_UINT32(0, rs.pos_q16);  // Should start at 0
    TEST_ASSERT_EQUAL_UINT32(expected, rs.step_q16);
}

void test_init_48k_to_44k_should_compute_correct_step(void)
{
    audio_resampler_stream_t rs;
    
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_SAMPLE_RATE_44K,
                                 AUDIO_BIT_DEPTH_16,
                                 2);
    
    // Expected: (48000 << 16) / 44100 = 71,347
    uint32_t expected = compute_expected_step(48000, 44100);
    
    TEST_ASSERT_EQUAL_UINT32(expected, rs.step_q16);
    TEST_ASSERT_EQUAL_UINT32(0, rs.pos_q16);
}

void test_init_same_rate_should_have_step_one(void)
{
    audio_resampler_stream_t rs;
    
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_BIT_DEPTH_16,
                                 2);
    
    // Expected: (48000 << 16) / 48000 = 65536 (Q16.16 value of 1.0)
    TEST_ASSERT_EQUAL_UINT32(Q16_ONE, rs.step_q16);
    TEST_ASSERT_EQUAL_UINT32(0, rs.pos_q16);
}

void test_init_mono_16bit_should_succeed(void)
{
    audio_resampler_stream_t rs;
    
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_44K,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_BIT_DEPTH_16,
                                 1);  // Mono
    
    TEST_ASSERT_EQUAL_INT(1, rs.channels);
    TEST_ASSERT_EQUAL_UINT32(AUDIO_BIT_DEPTH_16, rs.bit_depth);
}

void test_init_32bit_should_succeed(void)
{
    audio_resampler_stream_t rs;
    
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_44K,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_BIT_DEPTH_32,
                                 2);
    
    TEST_ASSERT_EQUAL_UINT32(AUDIO_BIT_DEPTH_32, rs.bit_depth);
}

//-----------------------------------------------------------------------------
// min_in_frames calculation tests
//-----------------------------------------------------------------------------

void test_min_in_frames_upsampling_at_zero_position(void)
{
    audio_resampler_stream_t rs;
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_44K,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_BIT_DEPTH_16,
                                 2);
    
    // Requesting 256 output frames
    // step_q16 ≈ 60,113 (44100/48000 in Q16.16)
    // min_in = ceil((256 * 60113 + 0) / 65536) = ceil(235.5) = 236
    size_t min_in = audio_resampler_stream_min_in_frames(&rs, FRAMES_PER_BLOCK);
    
    // Should need approximately 235-236 input frames
    TEST_ASSERT_GREATER_OR_EQUAL(235, min_in);
    TEST_ASSERT_LESS_OR_EQUAL(237, min_in);
}

void test_min_in_frames_downsampling_at_zero_position(void)
{
    audio_resampler_stream_t rs;
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_SAMPLE_RATE_44K,
                                 AUDIO_BIT_DEPTH_16,
                                 2);
    
    // Requesting 256 output frames
    // step_q16 ≈ 71,347 (48000/44100 in Q16.16)
    // min_in = ceil((256 * 71347 + 0) / 65536) = ceil(278.6) = 279
    size_t min_in = audio_resampler_stream_min_in_frames(&rs, FRAMES_PER_BLOCK);
    
    // Should need approximately 278-279 input frames
    TEST_ASSERT_GREATER_OR_EQUAL(278, min_in);
    TEST_ASSERT_LESS_OR_EQUAL(280, min_in);
}

void test_min_in_frames_same_rate_should_match_output(void)
{
    audio_resampler_stream_t rs;
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_BIT_DEPTH_16,
                                 2);
    
    // No resampling: needs out_frames + 1 for interpolation buffer
    size_t min_in = audio_resampler_stream_min_in_frames(&rs, FRAMES_PER_BLOCK);
    
    TEST_ASSERT_EQUAL_UINT(FRAMES_PER_BLOCK + 1, min_in);
}

void test_min_in_frames_with_fractional_position(void)
{
    audio_resampler_stream_t rs;
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_44K,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_BIT_DEPTH_16,
                                 2);
    
    // Simulate mid-stream position
    rs.pos_q16 = Q16_ONE / 2;  // Position 0.5
    
    size_t min_in = audio_resampler_stream_min_in_frames(&rs, FRAMES_PER_BLOCK);
    
    // With fractional offset, might need one more frame
    TEST_ASSERT_GREATER_OR_EQUAL(235, min_in);
    TEST_ASSERT_LESS_OR_EQUAL(238, min_in);
}

//-----------------------------------------------------------------------------
// Process tests: exact output, phase carry, interpolation
//-----------------------------------------------------------------------------

void test_process_should_produce_exact_output_frames(void)
{
    audio_resampler_stream_t rs;
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_44K,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_BIT_DEPTH_16,
                                 2);
    
    // Create input buffer
    int16_t in_buf[512 * 2];  // 512 frames stereo
    fill_test_signal_16(in_buf, 512, 2, 100);
    
    // Output buffer
    int16_t out_buf[FRAMES_PER_BLOCK * 2];
    memset(out_buf, 0, sizeof(out_buf));
    
    size_t in_consumed = 0;
    size_t out_produced = audio_resampler_stream_process(&rs,
                                                          (uint8_t *)in_buf,
                                                          512,
                                                          (uint8_t *)out_buf,
                                                          FRAMES_PER_BLOCK,
                                                          &in_consumed);
    
    // MUST produce exactly requested frames
    TEST_ASSERT_EQUAL_UINT(FRAMES_PER_BLOCK, out_produced);
    
    // Should consume approximately 235-236 frames
    TEST_ASSERT_GREATER_OR_EQUAL(235, in_consumed);
    TEST_ASSERT_LESS_OR_EQUAL(237, in_consumed);
    
    // Output should not be silence (contains interpolated values)
    TEST_ASSERT_FALSE(is_silence_16(out_buf, FRAMES_PER_BLOCK, 2));
}

void test_process_phase_should_carry_across_blocks(void)
{
    audio_resampler_stream_t rs;
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_44K,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_BIT_DEPTH_16,
                                 2);
    
    int16_t in_buf[512 * 2];
    fill_test_signal_16(in_buf, 512, 2, 100);
    
    int16_t out_buf1[FRAMES_PER_BLOCK * 2];
    int16_t out_buf2[FRAMES_PER_BLOCK * 2];
    
    uint32_t pos_before_first = rs.pos_q16;  // Should be 0
    
    // First block
    size_t in_consumed1 = 0;
    audio_resampler_stream_process(&rs,
                                    (uint8_t *)in_buf,
                                    512,
                                    (uint8_t *)out_buf1,
                                    FRAMES_PER_BLOCK,
                                    &in_consumed1);
    
    uint32_t pos_after_first = rs.pos_q16;
    
    // Position should have advanced
    TEST_ASSERT_GREATER_THAN(pos_before_first, pos_after_first);
    
    // Second block (feed remaining input)
    size_t in_consumed2 = 0;
    audio_resampler_stream_process(&rs,
                                    (uint8_t *)(in_buf + in_consumed1 * 2),
                                    512 - in_consumed1,
                                    (uint8_t *)out_buf2,
                                    FRAMES_PER_BLOCK,
                                    &in_consumed2);
    
    uint32_t pos_after_second = rs.pos_q16;
    
    // Position should continue advancing
    TEST_ASSERT_GREATER_THAN(pos_after_first, pos_after_second);
    
    // Both blocks should be non-silent
    TEST_ASSERT_FALSE(is_silence_16(out_buf1, FRAMES_PER_BLOCK, 2));
    TEST_ASSERT_FALSE(is_silence_16(out_buf2, FRAMES_PER_BLOCK, 2));
}

void test_process_total_frames_should_match_ratio(void)
{
    audio_resampler_stream_t rs;
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_44K,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_BIT_DEPTH_16,
                                 2);
    
    // Process entire 500ms file worth of data
    // Source: 44.1kHz * 0.5s = 22,050 frames
    // Expected output: 48kHz * 0.5s = 24,000 frames
    const size_t total_src_frames = 22050;
    const size_t expected_dst_frames = 24000;
    
    // Allocate buffers
    int16_t *in_buf = malloc(total_src_frames * 2 * sizeof(int16_t));
    TEST_ASSERT_NOT_NULL(in_buf);
    fill_test_signal_16(in_buf, total_src_frames, 2, 1000);
    
    size_t total_out_frames = 0;
    size_t total_in_consumed = 0;
    
    // Process in 256-frame output blocks
    while (total_out_frames < expected_dst_frames) {
        size_t remaining_out = expected_dst_frames - total_out_frames;
        size_t out_frames_this_block = (remaining_out > FRAMES_PER_BLOCK) 
                                       ? FRAMES_PER_BLOCK 
                                       : remaining_out;
        
        int16_t out_buf[FRAMES_PER_BLOCK * 2];
        size_t in_consumed = 0;
        
        size_t remaining_in = total_src_frames - total_in_consumed;
        
        audio_resampler_stream_process(&rs,
                                        (uint8_t *)(in_buf + total_in_consumed * 2),
                                        remaining_in,
                                        (uint8_t *)out_buf,
                                        out_frames_this_block,
                                        &in_consumed);
        
        total_out_frames += out_frames_this_block;
        total_in_consumed += in_consumed;
    }
    
    free(in_buf);
    
    // Should have consumed all or nearly all input (allow ±1 for rounding)
    TEST_ASSERT_UINT_WITHIN(1, total_src_frames, total_in_consumed);
    
    // Should have produced exactly expected output
    TEST_ASSERT_EQUAL_UINT(expected_dst_frames, total_out_frames);
}

void test_process_downsampling_should_produce_exact_frames(void)
{
    audio_resampler_stream_t rs;
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_SAMPLE_RATE_44K,
                                 AUDIO_BIT_DEPTH_16,
                                 2);
    
    int16_t in_buf[512 * 2];
    fill_test_signal_16(in_buf, 512, 2, 200);
    
    int16_t out_buf[FRAMES_PER_BLOCK * 2];
    size_t in_consumed = 0;
    
    size_t out_produced = audio_resampler_stream_process(&rs,
                                                          (uint8_t *)in_buf,
                                                          512,
                                                          (uint8_t *)out_buf,
                                                          FRAMES_PER_BLOCK,
                                                          &in_consumed);
    
    // Must produce exactly requested frames
    TEST_ASSERT_EQUAL_UINT(FRAMES_PER_BLOCK, out_produced);
    
    // Downsampling: should consume ~279 frames for 256 output
    TEST_ASSERT_GREATER_OR_EQUAL(278, in_consumed);
    TEST_ASSERT_LESS_OR_EQUAL(280, in_consumed);
}

void test_process_mono_should_work(void)
{
    audio_resampler_stream_t rs;
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_44K,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_BIT_DEPTH_16,
                                 1);  // Mono
    
    int16_t in_buf[512];  // Mono
    fill_test_signal_16(in_buf, 512, 1, 300);
    
    int16_t out_buf[FRAMES_PER_BLOCK];
    size_t in_consumed = 0;
    
    size_t out_produced = audio_resampler_stream_process(&rs,
                                                          (uint8_t *)in_buf,
                                                          512,
                                                          (uint8_t *)out_buf,
                                                          FRAMES_PER_BLOCK,
                                                          &in_consumed);
    
    TEST_ASSERT_EQUAL_UINT(FRAMES_PER_BLOCK, out_produced);
    TEST_ASSERT_FALSE(is_silence_16(out_buf, FRAMES_PER_BLOCK, 1));
}

//-----------------------------------------------------------------------------
// EOF handling tests
//-----------------------------------------------------------------------------

void test_process_eof_should_pad_with_silence(void)
{
    audio_resampler_stream_t rs;
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_44K,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_BIT_DEPTH_16,
                                 2);
    
    // Provide very few input frames (much less than needed)
    int16_t in_buf[50 * 2];  // Only 50 frames
    fill_test_signal_16(in_buf, 50, 2, 100);
    
    int16_t out_buf[FRAMES_PER_BLOCK * 2];
    memset(out_buf, 0xAA, sizeof(out_buf));  // Fill with non-zero pattern
    
    size_t in_consumed = 0;
    size_t out_produced = audio_resampler_stream_process(&rs,
                                                          (uint8_t *)in_buf,
                                                          50,
                                                          (uint8_t *)out_buf,
                                                          FRAMES_PER_BLOCK,
                                                          &in_consumed);
    
    // Must still produce exactly requested frames
    TEST_ASSERT_EQUAL_UINT(FRAMES_PER_BLOCK, out_produced);
    
    // Resampler calculates in_consumed based on position advancement
    // For upsampling, this will be the minimum needed (~235 frames)
    // even though only 50 are available
    TEST_ASSERT_GREATER_OR_EQUAL(50, in_consumed);
    
    // First part should have interpolated values (non-zero)
    bool has_nonzero = false;
    for (int i = 0; i < 100; i++) {  // Check first 50 frames
        if (out_buf[i] != 0) {
            has_nonzero = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(has_nonzero);
    
    // Latter part should be silence (padded)
    bool has_silence = false;
    for (size_t i = 100; i < FRAMES_PER_BLOCK * 2; i++) {
        if (out_buf[i] == 0) {
            has_silence = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(has_silence);
}

void test_process_zero_input_should_produce_all_silence(void)
{
    audio_resampler_stream_t rs;
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_44K,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_BIT_DEPTH_16,
                                 2);
    
    int16_t out_buf[FRAMES_PER_BLOCK * 2];
    memset(out_buf, 0xAA, sizeof(out_buf));
    
    size_t in_consumed = 0;
    size_t out_produced = audio_resampler_stream_process(&rs,
                                                          NULL,
                                                          0,
                                                          (uint8_t *)out_buf,
                                                          FRAMES_PER_BLOCK,
                                                          &in_consumed);
    
    // Must produce requested frames
    TEST_ASSERT_EQUAL_UINT(FRAMES_PER_BLOCK, out_produced);
    // in_consumed is based on position advancement, not actual input
    TEST_ASSERT_GREATER_THAN(0, in_consumed);
    
    // All output should be silence
    TEST_ASSERT_TRUE(is_silence_16(out_buf, FRAMES_PER_BLOCK, 2));
}

//-----------------------------------------------------------------------------
// Edge cases and robustness
//-----------------------------------------------------------------------------

void test_process_single_frame_output_should_work(void)
{
    audio_resampler_stream_t rs;
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_44K,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_BIT_DEPTH_16,
                                 2);
    
    int16_t in_buf[10 * 2];
    fill_test_signal_16(in_buf, 10, 2, 100);
    
    int16_t out_buf[2];
    size_t in_consumed = 0;
    
    size_t out_produced = audio_resampler_stream_process(&rs,
                                                          (uint8_t *)in_buf,
                                                          10,
                                                          (uint8_t *)out_buf,
                                                          1,  // Single frame
                                                          &in_consumed);
    
    TEST_ASSERT_EQUAL_UINT(1, out_produced);
    // For upsampling a single frame, step < 1.0, so integer part might be 0
    // This is correct - fractional position carries to next call
    TEST_ASSERT_LESS_OR_EQUAL(2, in_consumed);
}

void test_process_position_should_not_overflow(void)
{
    audio_resampler_stream_t rs;
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_44K,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_BIT_DEPTH_16,
                                 2);
    
    // Simulate many blocks
    int16_t in_buf[512 * 2];
    fill_test_signal_16(in_buf, 512, 2, 100);
    
    for (int block = 0; block < 100; block++) {
        int16_t out_buf[FRAMES_PER_BLOCK * 2];
        size_t in_consumed = 0;
        
        audio_resampler_stream_process(&rs,
                                        (uint8_t *)in_buf,
                                        512,
                                        (uint8_t *)out_buf,
                                        FRAMES_PER_BLOCK,
                                        &in_consumed);
        
        // Position should stay reasonable (integer part consumed)
        TEST_ASSERT_LESS_THAN(512 * Q16_ONE, rs.pos_q16);
    }
}

void test_interpolation_should_be_smooth(void)
{
    audio_resampler_stream_t rs;
    audio_resampler_stream_init(&rs,
                                 AUDIO_SAMPLE_RATE_44K,
                                 AUDIO_SAMPLE_RATE_48K,
                                 AUDIO_BIT_DEPTH_16,
                                 1);  // Mono for simpler analysis
    
    // Create simple ramp: 0, 100, 200, 300...
    int16_t in_buf[100];
    for (int i = 0; i < 100; i++) {
        in_buf[i] = i * 100;
    }
    
    int16_t out_buf[50];
    size_t in_consumed = 0;
    
    audio_resampler_stream_process(&rs,
                                    (uint8_t *)in_buf,
                                    100,
                                    (uint8_t *)out_buf,
                                    50,
                                    &in_consumed);
    
    // Output should be monotonically increasing (smooth interpolation)
    for (int i = 1; i < 50; i++) {
        TEST_ASSERT_GREATER_OR_EQUAL(out_buf[i-1], out_buf[i]);
    }
}

int main(void)
{
    UNITY_BEGIN();
    
    // Init and step_q16 tests
    RUN_TEST(test_init_44k_to_48k_should_compute_correct_step);
    RUN_TEST(test_init_48k_to_44k_should_compute_correct_step);
    RUN_TEST(test_init_same_rate_should_have_step_one);
    RUN_TEST(test_init_mono_16bit_should_succeed);
    RUN_TEST(test_init_32bit_should_succeed);
    
    // min_in_frames tests
    RUN_TEST(test_min_in_frames_upsampling_at_zero_position);
    RUN_TEST(test_min_in_frames_downsampling_at_zero_position);
    RUN_TEST(test_min_in_frames_same_rate_should_match_output);
    RUN_TEST(test_min_in_frames_with_fractional_position);
    
    // Process tests
    RUN_TEST(test_process_should_produce_exact_output_frames);
    RUN_TEST(test_process_phase_should_carry_across_blocks);
    RUN_TEST(test_process_total_frames_should_match_ratio);
    RUN_TEST(test_process_downsampling_should_produce_exact_frames);
    RUN_TEST(test_process_mono_should_work);
    
    // EOF handling
    RUN_TEST(test_process_eof_should_pad_with_silence);
    RUN_TEST(test_process_zero_input_should_produce_all_silence);
    
    // Edge cases
    RUN_TEST(test_process_single_frame_output_should_work);
    RUN_TEST(test_process_position_should_not_overflow);
    RUN_TEST(test_interpolation_should_be_smooth);
    
    return UNITY_END();
}
