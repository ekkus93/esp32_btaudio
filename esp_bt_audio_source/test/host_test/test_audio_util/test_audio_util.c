#include "unity.h"

#include <string.h>

#include "audio_util.h"
#include "esp_err.h"

void setUp(void) {}
void tearDown(void) {}

void test_convert_16_to_32_should_shift_left(void)
{
    int16_t src[] = {0x1234, (int16_t)-0x1234, 0x7fff};
    int32_t dst[3] = {0};
    size_t dst_size = 0;

    audio_convert_args_t args = {
        .src = src,
        .dst = dst,
        .src_size = sizeof(src),
        .src_bit_depth = AUDIO_BIT_DEPTH_16,
        .dst_bit_depth = AUDIO_BIT_DEPTH_32,
        .dst_size = &dst_size,
        .work_bytes = 0,
    };

    TEST_ASSERT_EQUAL(ESP_OK, convert_audio_format(&args));
    TEST_ASSERT_EQUAL(sizeof(dst), dst_size);
    TEST_ASSERT_EQUAL_INT32(0x12340000, dst[0]);
    TEST_ASSERT_EQUAL_INT32((int32_t)(-0x1234) << 16, dst[1]);
    TEST_ASSERT_EQUAL_INT32(0x7fff0000, dst[2]);
}

void test_convert_should_truncate_to_work_bytes(void)
{
    int16_t src[] = {1, 2, 3, 4};
    int16_t dst[4] = {0};
    size_t dst_size = 0;

    audio_convert_args_t args = {
        .src = src,
        .dst = dst,
        .src_size = sizeof(src),
        .src_bit_depth = AUDIO_BIT_DEPTH_16,
        .dst_bit_depth = AUDIO_BIT_DEPTH_16,
        .dst_size = &dst_size,
        .work_bytes = 4,
    };

    TEST_ASSERT_EQUAL(ESP_OK, convert_audio_format(&args));
    TEST_ASSERT_EQUAL_size_t(4, dst_size);
    TEST_ASSERT_EQUAL_INT16(src[0], dst[0]);
    TEST_ASSERT_EQUAL_INT16(src[1], dst[1]);
}

void test_resample_downsample_should_pick_first_and_last(void)
{
    int16_t src[] = {1000, 2000, 3000, 4000};
    int16_t dst[4] = {0};
    size_t dst_size = 0;

    audio_resample_args_t args = {
        .src = src,
        .dst = dst,
        .src_size = sizeof(src),
        .src_rate = AUDIO_SAMPLE_RATE_32K,
        .dst_rate = AUDIO_SAMPLE_RATE_16K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .dst_size = &dst_size,
        .work_bytes = sizeof(dst),
    };

    TEST_ASSERT_EQUAL(ESP_OK, resample_audio(&args));
    TEST_ASSERT_EQUAL_size_t(4, dst_size);
    TEST_ASSERT_EQUAL_INT16(1000, dst[0]);
    TEST_ASSERT_EQUAL_INT16(4000, dst[1]);
}

void test_resample_upsample_should_interpolate_linearly(void)
{
    int16_t src[] = {0, 1000};
    int16_t dst[8] = {0};
    size_t dst_size = 0;

    audio_resample_args_t args = {
        .src = src,
        .dst = dst,
        .src_size = sizeof(src),
        .src_rate = AUDIO_SAMPLE_RATE_16K,
        .dst_rate = AUDIO_SAMPLE_RATE_32K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .dst_size = &dst_size,
        .work_bytes = sizeof(dst),
    };

    TEST_ASSERT_EQUAL(ESP_OK, resample_audio(&args));
    TEST_ASSERT_EQUAL_size_t(sizeof(src), dst_size);
    TEST_ASSERT_EQUAL_INT16(0, dst[0]);
    TEST_ASSERT_EQUAL_INT16(1000, dst[1]);
}

/* ============================================================================
 * Edge Case Tests - NULL pointer validations
 * ========================================================================= */

void test_convert_null_args_should_fail(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, convert_audio_format(NULL));
}

void test_convert_null_dst_should_fail(void) {
    int16_t src[] = {1, 2};
    size_t dst_size = 0;
    
    audio_convert_args_t args = {
        .src = src,
        .dst = NULL,
        .src_size = sizeof(src),
        .src_bit_depth = AUDIO_BIT_DEPTH_16,
        .dst_bit_depth = AUDIO_BIT_DEPTH_16,
        .dst_size = &dst_size,
        .work_bytes = 0,
    };
    
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, convert_audio_format(&args));
}

void test_convert_null_src_should_fail(void) {
    int16_t dst[2] = {0};
    size_t dst_size = 0;
    
    audio_convert_args_t args = {
        .src = NULL,
        .dst = dst,
        .src_size = 4,
        .src_bit_depth = AUDIO_BIT_DEPTH_16,
        .dst_bit_depth = AUDIO_BIT_DEPTH_16,
        .dst_size = &dst_size,
        .work_bytes = 0,
    };
    
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, convert_audio_format(&args));
}

void test_convert_null_dst_size_should_fail(void) {
    int16_t src[] = {1, 2};
    int16_t dst[2] = {0};
    
    audio_convert_args_t args = {
        .src = src,
        .dst = dst,
        .src_size = sizeof(src),
        .src_bit_depth = AUDIO_BIT_DEPTH_16,
        .dst_bit_depth = AUDIO_BIT_DEPTH_16,
        .dst_size = NULL,
        .work_bytes = 0,
    };
    
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, convert_audio_format(&args));
}

/* ============================================================================
 * Edge Case Tests - Zero-size inputs
 * ========================================================================= */

void test_convert_zero_src_size_should_copy_nothing(void) {
    int16_t src[] = {1, 2};
    int16_t dst[2] = {0};
    size_t dst_size = 0;
    
    audio_convert_args_t args = {
        .src = src,
        .dst = dst,
        .src_size = 0,
        .src_bit_depth = AUDIO_BIT_DEPTH_16,
        .dst_bit_depth = AUDIO_BIT_DEPTH_16,
        .dst_size = &dst_size,
        .work_bytes = 0,
    };
    
    TEST_ASSERT_EQUAL(ESP_OK, convert_audio_format(&args));
    TEST_ASSERT_EQUAL(0, dst_size);
}

/* ============================================================================
 * Edge Case Tests - Unsupported format conversions
 * ========================================================================= */

void test_convert_32_to_24_should_fail_unsupported(void) {
    int32_t src[] = {0x12345678, -0x12345678};
    int32_t dst[2] = {0};
    size_t dst_size = 0;
    
    audio_convert_args_t args = {
        .src = src,
        .dst = dst,
        .src_size = sizeof(src),
        .src_bit_depth = AUDIO_BIT_DEPTH_32,
        .dst_bit_depth = AUDIO_BIT_DEPTH_24,
        .dst_size = &dst_size,
        .work_bytes = 0,
    };
    
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, convert_audio_format(&args));
}

void test_convert_24_to_32_should_fail_unsupported(void) {
    int32_t src[] = {0x12345678};
    int32_t dst[2] = {0};
    size_t dst_size = 0;
    
    audio_convert_args_t args = {
        .src = src,
        .dst = dst,
        .src_size = sizeof(src),
        .src_bit_depth = AUDIO_BIT_DEPTH_24,
        .dst_bit_depth = AUDIO_BIT_DEPTH_32,
        .dst_size = &dst_size,
        .work_bytes = 0,
    };
    
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, convert_audio_format(&args));
}

/* ============================================================================
 * Edge Case Tests - Resample with NULL/zero validations
 * ========================================================================= */

void test_resample_null_args_should_fail(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, resample_audio(NULL));
}

void test_resample_null_dst_should_fail(void) {
    int16_t src[] = {1000, 2000};
    size_t dst_size = 0;
    
    audio_resample_args_t args = {
        .src = src,
        .dst = NULL,
        .src_size = sizeof(src),
        .src_rate = AUDIO_SAMPLE_RATE_16K,
        .dst_rate = AUDIO_SAMPLE_RATE_32K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .dst_size = &dst_size,
        .work_bytes = 8,
    };
    
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, resample_audio(&args));
}

void test_resample_zero_src_rate_should_fail(void) {
    int16_t src[] = {1000, 2000};
    int16_t dst[4] = {0};
    size_t dst_size = 0;
    
    audio_resample_args_t args = {
        .src = src,
        .dst = dst,
        .src_size = sizeof(src),
        .src_rate = 0,
        .dst_rate = AUDIO_SAMPLE_RATE_32K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .dst_size = &dst_size,
        .work_bytes = sizeof(dst),
    };
    
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, resample_audio(&args));
}

void test_resample_zero_src_size_should_succeed_with_nothing(void) {
    int16_t src[] = {1000, 2000};
    int16_t dst[4] = {0};
    size_t dst_size = 0;
    
    audio_resample_args_t args = {
        .src = src,
        .dst = dst,
        .src_size = 0,
        .src_rate = AUDIO_SAMPLE_RATE_16K,
        .dst_rate = AUDIO_SAMPLE_RATE_32K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .dst_size = &dst_size,
        .work_bytes = sizeof(dst),
    };
    
    TEST_ASSERT_EQUAL(ESP_OK, resample_audio(&args));
    TEST_ASSERT_EQUAL(0, dst_size);
}

/* I2S-link regression probes (2026-07-11 bring-up): the capture path feeds
 * extracted s16 44.1kHz stereo through convert(16->16) + resample(equal
 * rates). During bring-up this stage was suspected of mangling the stream
 * (one channel zeroed); these identity tests pin the actual behavior. */

void test_convert_16_to_16_stereo_is_identity(void)
{
    int16_t src[64];
    int16_t dst[64];
    size_t dst_size = 0;
    for (int i = 0; i < 64; i++) {
        src[i] = (int16_t)(101 * i - 3000);  /* sign-mixed ramp */
    }
    memset(dst, 0x5A, sizeof(dst));

    audio_convert_args_t args = {
        .src = src,
        .dst = dst,
        .src_size = sizeof(src),
        .src_bit_depth = AUDIO_BIT_DEPTH_16,
        .dst_bit_depth = AUDIO_BIT_DEPTH_16,
        .dst_size = &dst_size,
        .work_bytes = sizeof(dst),
    };

    TEST_ASSERT_EQUAL(ESP_OK, convert_audio_format(&args));
    TEST_ASSERT_EQUAL(sizeof(src), dst_size);
    TEST_ASSERT_EQUAL_INT16_ARRAY(src, dst, 64);
}

void test_resample_equal_rates_stereo_is_identity(void)
{
    int16_t src[64];  /* 32 stereo frames, L != R */
    int16_t dst[64];
    size_t dst_size = 0;
    for (int f = 0; f < 32; f++) {
        src[2 * f] = (int16_t)(100 * f - 1000);
        src[2 * f + 1] = (int16_t)(-200 * f + 500);
    }
    memset(dst, 0x5A, sizeof(dst));

    audio_resample_args_t args = {
        .src = src,
        .dst = dst,
        .src_size = sizeof(src),
        .src_rate = AUDIO_SAMPLE_RATE_44K,
        .dst_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .dst_size = &dst_size,
        .work_bytes = sizeof(dst),
    };

    TEST_ASSERT_EQUAL(ESP_OK, resample_audio(&args));
    TEST_ASSERT_EQUAL(sizeof(src), dst_size);
    TEST_ASSERT_EQUAL_INT16_ARRAY(src, dst, 64);
}

void test_convert_then_resample_chain_is_identity(void)
{
    /* The exact shape the I2S fill used pre-bypass: s16 stereo through both
     * stages back-to-back. */
    int16_t src[64];
    int16_t mid[64];
    int16_t dst[64];
    size_t mid_size = 0;
    size_t dst_size = 0;
    for (int f = 0; f < 32; f++) {
        src[2 * f] = (int16_t)(100 * f - 1000);
        src[2 * f + 1] = (int16_t)(-200 * f + 500);
    }

    audio_convert_args_t cargs = {
        .src = src,
        .dst = mid,
        .src_size = sizeof(src),
        .src_bit_depth = AUDIO_BIT_DEPTH_16,
        .dst_bit_depth = AUDIO_BIT_DEPTH_16,
        .dst_size = &mid_size,
        .work_bytes = sizeof(mid),
    };
    TEST_ASSERT_EQUAL(ESP_OK, convert_audio_format(&cargs));

    audio_resample_args_t rargs = {
        .src = mid,
        .dst = dst,
        .src_size = mid_size,
        .src_rate = AUDIO_SAMPLE_RATE_44K,
        .dst_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .dst_size = &dst_size,
        .work_bytes = sizeof(dst),
    };
    TEST_ASSERT_EQUAL(ESP_OK, resample_audio(&rargs));
    TEST_ASSERT_EQUAL(sizeof(src), dst_size);
    TEST_ASSERT_EQUAL_INT16_ARRAY(src, dst, 64);
}

/* --- audio_engine_hold_for_live_i2s: the anti-silence-stuffing policy that
 * killed the ~60% zero interleave (harsh chop) on the live I2S path. --- */

void test_hold_true_only_for_empty_live_i2s(void)
{
    /* The one case that must hold: I2S source, running, produced nothing. */
    TEST_ASSERT_TRUE(audio_engine_hold_for_live_i2s(0, true, true));
}

void test_hold_false_when_bytes_produced(void)
{
    /* Real data this tick — never hold, even for a running I2S source. */
    TEST_ASSERT_FALSE(audio_engine_hold_for_live_i2s(1024, true, true));
    TEST_ASSERT_FALSE(audio_engine_hold_for_live_i2s(2, true, true));
}

void test_hold_false_when_not_i2s_source(void)
{
    /* Other sources (synth/uart): produced==0 is a genuine underrun -> silence
     * fallback must run, so the policy must NOT hold. */
    TEST_ASSERT_FALSE(audio_engine_hold_for_live_i2s(0, false, true));
}

void test_hold_false_when_i2s_not_running(void)
{
    /* I2S selected by stat comparison but manager stopped: no real-time source
     * to wait for, so silence is correct -> do not hold. */
    TEST_ASSERT_FALSE(audio_engine_hold_for_live_i2s(0, true, false));
    /* And the fully-idle case. */
    TEST_ASSERT_FALSE(audio_engine_hold_for_live_i2s(0, false, false));
}

int main(void)
{
    UNITY_BEGIN();
    
    /* Original tests */
    RUN_TEST(test_convert_16_to_32_should_shift_left);
    RUN_TEST(test_convert_should_truncate_to_work_bytes);
    RUN_TEST(test_resample_downsample_should_pick_first_and_last);
    RUN_TEST(test_resample_upsample_should_interpolate_linearly);
    
    /* NULL pointer tests */
    RUN_TEST(test_convert_null_args_should_fail);
    RUN_TEST(test_convert_null_dst_should_fail);
    RUN_TEST(test_convert_null_src_should_fail);
    RUN_TEST(test_convert_null_dst_size_should_fail);
    
    /* Zero-size tests */
    RUN_TEST(test_convert_zero_src_size_should_copy_nothing);
    
    /* Unsupported format tests */
    RUN_TEST(test_convert_32_to_24_should_fail_unsupported);
    RUN_TEST(test_convert_24_to_32_should_fail_unsupported);
    
    /* Resample edge case tests */
    RUN_TEST(test_resample_null_args_should_fail);
    RUN_TEST(test_resample_null_dst_should_fail);
    RUN_TEST(test_resample_zero_src_rate_should_fail);
    RUN_TEST(test_resample_zero_src_size_should_succeed_with_nothing);

    /* I2S-link regression probes */
    RUN_TEST(test_convert_16_to_16_stereo_is_identity);
    RUN_TEST(test_resample_equal_rates_stereo_is_identity);
    RUN_TEST(test_convert_then_resample_chain_is_identity);

    RUN_TEST(test_hold_true_only_for_empty_live_i2s);
    RUN_TEST(test_hold_false_when_bytes_produced);
    RUN_TEST(test_hold_false_when_not_i2s_source);
    RUN_TEST(test_hold_false_when_i2s_not_running);
    
    return UNITY_END();
}
