#include "unity.h"
#include "pcm_processing.h"
#include "esp_err.h"
#include <string.h>

static void test_endian_swaps(void)
{
    int16_t samples[3] = {0x1122, (int16_t)0xFF00, 0x7F01};
    int16_t expect_swap[3];
    for (int i = 0; i < 3; ++i) {
        expect_swap[i] = (int16_t)(((samples[i] & 0xFF) << 8) | ((samples[i] >> 8) & 0xFF));
    }

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pcm_convert_to_big_endian(NULL, 2));
    TEST_ASSERT_EQUAL(ESP_OK, pcm_convert_to_big_endian(samples, 3));
    TEST_ASSERT_EQUAL_INT16_ARRAY(expect_swap, samples, 3);

    TEST_ASSERT_EQUAL(ESP_OK, pcm_convert_to_little_endian(samples, 3));
    /* Swapping twice returns to original */
    int16_t re_swapped[3];
    for (int i = 0; i < 3; ++i) {
        re_swapped[i] = (int16_t)(((expect_swap[i] & 0xFF) << 8) | ((expect_swap[i] >> 8) & 0xFF));
    }
    TEST_ASSERT_EQUAL_INT16_ARRAY(re_swapped, samples, 3);
}

static void test_bit_depth_conversions(void)
{
    int16_t src16[3] = {0x1234, -0x1234, 0x7F00};
    int32_t dst24[3] = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pcm_convert_16bit_to_24bit(NULL, dst24, 3));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pcm_convert_16bit_to_24bit(src16, NULL, 3));
    TEST_ASSERT_EQUAL(ESP_OK, pcm_convert_16bit_to_24bit(src16, dst24, 3));
    TEST_ASSERT_EQUAL_INT32((int32_t)src16[0] << 8, dst24[0]);
    TEST_ASSERT_EQUAL_INT32((int32_t)src16[1] << 8, dst24[1]);

    int16_t roundtrip[3] = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pcm_convert_24bit_to_16bit(NULL, roundtrip, 3));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pcm_convert_24bit_to_16bit(dst24, NULL, 3));
    TEST_ASSERT_EQUAL(ESP_OK, pcm_convert_24bit_to_16bit(dst24, roundtrip, 3));
    TEST_ASSERT_EQUAL_INT16(src16[0], roundtrip[0]);
    TEST_ASSERT_EQUAL_INT16(src16[1], roundtrip[1]);
}

static void test_channel_conversions(void)
{
    int16_t stereo[4] = {1000, -1000, 5000, 5000}; /* two frames */
    int16_t mono[2] = {0};
    convert_stereo_to_mono(stereo, mono, 2);
    TEST_ASSERT_EQUAL_INT16(0, mono[0]);
    TEST_ASSERT_EQUAL_INT16(5000, mono[1]);

    int16_t stereo_out[4] = {0};
    convert_mono_to_stereo(mono, stereo_out, 2);
    int16_t expect_stereo[4] = {0, 0, 5000, 5000};
    TEST_ASSERT_EQUAL_INT16_ARRAY(expect_stereo, stereo_out, 4);
}

static void test_pcm_convert_endianness(void)
{
    int16_t sample = 0x1234;
    int16_t swapped = pcm_convert_endianness(sample);
    TEST_ASSERT_EQUAL_HEX16(0x3412, swapped);
}

void pcm_processing_tests_register(void)
{
    RUN_TEST(test_endian_swaps);
    RUN_TEST(test_bit_depth_conversions);
    RUN_TEST(test_channel_conversions);
    RUN_TEST(test_pcm_convert_endianness);
}
