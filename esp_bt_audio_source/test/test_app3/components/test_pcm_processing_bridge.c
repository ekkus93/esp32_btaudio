/* Device-side audio_util coverage using the production conversion helpers. */

#include "unity.h"
#include "audio_util.h"
#include <string.h>

static void test_convert_16_to_24_and_back(void)
{
	int16_t src16[3] = {0x1234, (int16_t)-0x1234, 0x7f00};
	int32_t dst24[3] = {0};
	size_t dst_size = 0;

	audio_convert_args_t to24 = {
		.src = src16,
		.dst = dst24,
		.src_size = sizeof(src16),
		.src_bit_depth = AUDIO_BIT_DEPTH_16,
		.dst_bit_depth = AUDIO_BIT_DEPTH_24,
		.dst_size = &dst_size,
		.work_bytes = 0,
	};
	TEST_ASSERT_EQUAL(ESP_OK, convert_audio_format(&to24));
	TEST_ASSERT_EQUAL_size_t(sizeof(dst24), dst_size);
	TEST_ASSERT_EQUAL_INT32(((int32_t)src16[0]) << 8, dst24[0]);

	int16_t back16[3] = {0};
	audio_convert_args_t to16 = {
		.src = dst24,
		.dst = back16,
		.src_size = dst_size,
		.src_bit_depth = AUDIO_BIT_DEPTH_24,
		.dst_bit_depth = AUDIO_BIT_DEPTH_16,
		.dst_size = &dst_size,
		.work_bytes = 0,
	};
	TEST_ASSERT_EQUAL(ESP_OK, convert_audio_format(&to16));
	TEST_ASSERT_EQUAL_INT16_ARRAY(src16, back16, 3);
}

static void test_convert_truncates_to_work_bytes(void)
{
	int16_t src[4] = {1, 2, 3, 4};
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

static void test_resample_downsample_and_upsample(void)
{
	int16_t src_down[] = {1000, 2000, 3000, 4000};
	int16_t dst_down[4] = {0};
	size_t dst_size = 0;

	audio_resample_args_t down_args = {
		.src = src_down,
		.dst = dst_down,
		.src_size = sizeof(src_down),
		.src_rate = AUDIO_SAMPLE_RATE_32K,
		.dst_rate = AUDIO_SAMPLE_RATE_16K,
		.bit_depth = AUDIO_BIT_DEPTH_16,
		.channels = AUDIO_CHANNEL_MONO,
		.dst_size = &dst_size,
		.work_bytes = sizeof(dst_down),
	};
	TEST_ASSERT_EQUAL(ESP_OK, resample_audio(&down_args));
	TEST_ASSERT_EQUAL_size_t(4, dst_size);
	TEST_ASSERT_EQUAL_INT16(1000, dst_down[0]);
	TEST_ASSERT_EQUAL_INT16(4000, dst_down[1]);

	int16_t src_up[] = {0, 1000};
	int16_t dst_up[4] = {0};
	dst_size = 0;
	audio_resample_args_t up_args = {
		.src = src_up,
		.dst = dst_up,
		.src_size = sizeof(src_up),
		.src_rate = AUDIO_SAMPLE_RATE_16K,
		.dst_rate = AUDIO_SAMPLE_RATE_32K,
		.bit_depth = AUDIO_BIT_DEPTH_16,
		.channels = AUDIO_CHANNEL_MONO,
		.dst_size = &dst_size,
		.work_bytes = sizeof(dst_up),
	};
	TEST_ASSERT_EQUAL(ESP_OK, resample_audio(&up_args));
	TEST_ASSERT_EQUAL_size_t(sizeof(src_up), dst_size);
	TEST_ASSERT_EQUAL_INT16(0, dst_up[0]);
	TEST_ASSERT_EQUAL_INT16(1000, dst_up[1]);
}

void audio_util_tests_register(void)
{
	RUN_TEST(test_convert_16_to_24_and_back);
	RUN_TEST(test_convert_truncates_to_work_bytes);
	RUN_TEST(test_resample_downsample_and_upsample);
}
