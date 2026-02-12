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
    
    return UNITY_END();
}
