/**
 * PCM Format Tests
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "unity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "pcm_processing.h"
#include "audio_test_helpers.h"
#include "esp_err.h"

static const char *TAG = "PCM_FORMAT_TEST";

#define TEST_BUFFER_SIZE 1024

static int16_t test_buffer_16bit[TEST_BUFFER_SIZE];
static uint8_t test_buffer_24bit[TEST_BUFFER_SIZE * 3];
static int16_t result_buffer_16bit[TEST_BUFFER_SIZE];

static void test_pcm_16bit_format(void)
{
    ESP_LOGI(TAG, "Testing 16-bit PCM format");
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        test_buffer_16bit[i] = (i % 256) * 128;
    }
    TEST_ASSERT_EQUAL_INT16(0, test_buffer_16bit[0]);
    TEST_ASSERT_EQUAL_INT16(128, test_buffer_16bit[1]);
    TEST_ASSERT_EQUAL_INT16(256, test_buffer_16bit[2]);
    float rms = calculate_rms(test_buffer_16bit, TEST_BUFFER_SIZE, 0, 1);
    TEST_ASSERT_TRUE_MESSAGE(rms > 0.0f, "RMS value should be positive for 16-bit PCM ramp");
    test_buffer_16bit[0] = 12345;
    TEST_ASSERT_EQUAL_INT16(12345, test_buffer_16bit[0]);
}

static void test_pcm_24bit_format(void)
{
    ESP_LOGI(TAG, "Testing 24-bit PCM format");
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        test_buffer_16bit[i] = (i % 256) * 128;
    }
    test_convert_16bit_to_24bit(test_buffer_16bit, test_buffer_24bit, TEST_BUFFER_SIZE);
    TEST_ASSERT_NOT_EQUAL(0, test_buffer_24bit[0] | test_buffer_24bit[1] | test_buffer_24bit[2]);
    test_convert_24bit_to_16bit(test_buffer_24bit, result_buffer_16bit, TEST_BUFFER_SIZE);
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        TEST_ASSERT_INT16_WITHIN(2, test_buffer_16bit[i], result_buffer_16bit[i]);
    }
}

static void test_pcm_sample_scaling(void)
{
    ESP_LOGI(TAG, "Testing PCM sample scaling");
    int16_t samples_16bit[4] = {0, 16384, -16384, 32767};
    uint8_t samples_24bit[12];
    test_convert_16bit_to_24bit(samples_16bit, samples_24bit, 4);
    int16_t result_samples[4];
    test_convert_24bit_to_16bit(samples_24bit, result_samples, 4);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_INT16_WITHIN(2, samples_16bit[i], result_samples[i]);
    }
}

static void test_pcm_bit_depth_conversion(void)
{
    ESP_LOGI(TAG, "Testing PCM bit depth conversion");
    generate_test_tone(test_buffer_16bit, TEST_BUFFER_SIZE, 440.0f, 44100.0f, 16384);
    test_convert_16bit_to_24bit(test_buffer_16bit, test_buffer_24bit, TEST_BUFFER_SIZE);
    test_convert_24bit_to_16bit(test_buffer_24bit, result_buffer_16bit, TEST_BUFFER_SIZE);
    float original_rms = calculate_rms(test_buffer_16bit, TEST_BUFFER_SIZE, 0, 1);
    float result_rms = calculate_rms(result_buffer_16bit, TEST_BUFFER_SIZE, 0, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, original_rms, result_rms);
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        TEST_ASSERT_INT16_WITHIN(2, test_buffer_16bit[i], result_buffer_16bit[i]);
    }
}

void app_main_pcm_format_tests(void)
{
    ESP_LOGI(TAG, "Starting PCM format tests");
    RUN_TEST(test_pcm_16bit_format);
    RUN_TEST(test_pcm_24bit_format);
    RUN_TEST(test_pcm_sample_scaling);
    RUN_TEST(test_pcm_bit_depth_conversion);
    ESP_LOGI(TAG, "PCM format tests completed");
}
