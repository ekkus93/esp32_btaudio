/**
 * I2S Channel Tests
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "unity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "audio_test_helpers.h"
#include "i2s_audio.h"
#include "esp_err.h"

static const char *TAG = "I2S_CHANNEL_TEST";

#define TEST_BUFFER_SIZE 1024

static int16_t stereo_buffer[TEST_BUFFER_SIZE * 2];
static int16_t mono_buffer[TEST_BUFFER_SIZE];
static int16_t result_buffer[TEST_BUFFER_SIZE * 2];

static void test_stereo_buffer_format(void)
{
    ESP_LOGI(TAG, "Testing stereo buffer format");
    generate_stereo_test_tone(stereo_buffer, TEST_BUFFER_SIZE, 440.0f, 880.0f, 44100.0f, 16384);
    TEST_ASSERT_NOT_EQUAL(stereo_buffer[0], stereo_buffer[1]);
    TEST_ASSERT_NOT_EQUAL(stereo_buffer[2], stereo_buffer[3]);
    float left_rms = calculate_rms(stereo_buffer, TEST_BUFFER_SIZE * 2, 0, 2);
    float right_rms = calculate_rms(stereo_buffer, TEST_BUFFER_SIZE * 2, 1, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, left_rms, right_rms);
    TEST_ASSERT_TRUE_MESSAGE(left_rms > 0.0f, "left channel RMS should be positive");
    TEST_ASSERT_TRUE_MESSAGE(right_rms > 0.0f, "right channel RMS should be positive");
}

static void test_mono_buffer_format(void)
{
    ESP_LOGI(TAG, "Testing mono buffer format");
    generate_test_tone(mono_buffer, TEST_BUFFER_SIZE, 440.0f, 44100.0f, 16384);
    TEST_ASSERT_NOT_EQUAL(0, mono_buffer[0] | mono_buffer[1] | mono_buffer[2]);
    float rms = calculate_rms(mono_buffer, TEST_BUFFER_SIZE, 0, 1);
    TEST_ASSERT_TRUE_MESSAGE(rms > 0.0f, "mono RMS should be positive");
}

static void test_stereo_to_mono_conversion(void)
{
    ESP_LOGI(TAG, "Testing stereo to mono conversion");
    generate_stereo_test_tone(stereo_buffer, TEST_BUFFER_SIZE, 440.0f, 440.0f, 44100.0f, 16384);
    test_convert_stereo_to_mono(stereo_buffer, mono_buffer, TEST_BUFFER_SIZE);
    for (int i = 0; i < 10; i++) {
        int32_t expected = ((int32_t)stereo_buffer[i * 2] + (int32_t)stereo_buffer[i * 2 + 1]) / 2;
        TEST_ASSERT_INT16_WITHIN(1, expected, mono_buffer[i]);
    }
    float left_rms = calculate_rms(stereo_buffer, TEST_BUFFER_SIZE * 2, 0, 2);
    float right_rms = calculate_rms(stereo_buffer, TEST_BUFFER_SIZE * 2, 1, 2);
    float mono_rms = calculate_rms(mono_buffer, TEST_BUFFER_SIZE, 0, 1);
    float avg_stereo_rms = (left_rms + right_rms) / 2.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.1f, avg_stereo_rms, mono_rms);
}

static void test_mono_to_stereo_conversion(void)
{
    ESP_LOGI(TAG, "Testing mono to stereo conversion");
    generate_test_tone(mono_buffer, TEST_BUFFER_SIZE / 2, 440.0f, 44100.0f, 16384);
    test_convert_mono_to_stereo(mono_buffer, result_buffer, TEST_BUFFER_SIZE / 2);
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_INT16(result_buffer[i * 2], result_buffer[i * 2 + 1]);
        TEST_ASSERT_EQUAL_INT16(mono_buffer[i], result_buffer[i * 2]);
    }
    float mono_rms = calculate_rms(mono_buffer, TEST_BUFFER_SIZE / 2, 0, 1);
    float left_rms = calculate_rms(result_buffer, TEST_BUFFER_SIZE, 0, 2);
    float right_rms = calculate_rms(result_buffer, TEST_BUFFER_SIZE, 1, 2);
    TEST_ASSERT_EQUAL_FLOAT(left_rms, right_rms);
    TEST_ASSERT_EQUAL_FLOAT(mono_rms, left_rms);
}

static void test_stereo_mono_round_trip(void)
{
    ESP_LOGI(TAG, "Testing stereo->mono->stereo round trip");
    generate_stereo_test_tone(stereo_buffer, TEST_BUFFER_SIZE, 440.0f, 440.0f, 44100.0f, 16384);
    test_convert_stereo_to_mono(stereo_buffer, mono_buffer, TEST_BUFFER_SIZE);
    test_convert_mono_to_stereo(mono_buffer, result_buffer, TEST_BUFFER_SIZE);
    for (int i = 0; i < TEST_BUFFER_SIZE * 2; i++) {
        TEST_ASSERT_INT16_WITHIN(1, stereo_buffer[i], result_buffer[i]);
    }
}

void app_main_i2s_channel_tests(void)
{
    ESP_LOGI(TAG, "Starting I2S channel tests");
    RUN_TEST(test_stereo_buffer_format);
    RUN_TEST(test_mono_buffer_format);
    RUN_TEST(test_stereo_to_mono_conversion);
    RUN_TEST(test_mono_to_stereo_conversion);
    RUN_TEST(test_stereo_mono_round_trip);
    ESP_LOGI(TAG, "I2S channel tests completed");
}
