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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_convert_16_to_32_should_shift_left);
    RUN_TEST(test_convert_should_truncate_to_work_bytes);
    RUN_TEST(test_resample_downsample_should_pick_first_and_last);
    RUN_TEST(test_resample_upsample_should_interpolate_linearly);
    return UNITY_END();
}
