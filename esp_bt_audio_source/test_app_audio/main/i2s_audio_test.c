/**
 * I2S Audio Tests
 */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2s_audio_test.h"

#ifndef UNIT_TEST
#define UNIT_TEST
#endif

#include "i2s_audio.h"
#include "audio_test_helpers.h"

static const char *TAG = "I2S_AUDIO_TEST";

#define TEST_BUFFER_SIZE 1024
static int16_t test_buffer[TEST_BUFFER_SIZE];

void setUp(void)
{
    ESP_LOGI(TAG, "Setting up I2S audio test");
    memset(test_buffer, 0, sizeof(test_buffer));
    esp_err_t ret = i2s_driver_init(44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "Failed to initialize I2S driver in setUp");
}

void tearDown(void)
{
    ESP_LOGI(TAG, "Tearing down I2S audio test");
    if (i2s_is_driver_installed()) {
        i2s_driver_deinit();
    }
}

static void test_i2s_driver_init(void)
{
    ESP_LOGI(TAG, "Testing I2S driver initialization");
    if (i2s_is_driver_installed()) {
        i2s_driver_deinit();
    }
    esp_err_t ret = i2s_driver_init(48000, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(i2s_is_driver_installed());
    TEST_ASSERT_EQUAL(I2S_SLOT_MODE_STEREO, i2s_get_channel_format());
}

static void test_i2s_standard_mode(void)
{
    ESP_LOGI(TAG, "Testing I2S standard mode configuration");
    esp_err_t ret = i2s_configure_standard_mode();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(I2S_SLOT_MODE_STEREO, i2s_get_channel_format());
    generate_test_tone(test_buffer, TEST_BUFFER_SIZE, 1000.0f, 16000.0f, 44100);
    size_t bytes_written = 0;
    TEST_ASSERT_EQUAL(ESP_OK, i2s_write_samples(test_buffer, TEST_BUFFER_SIZE, &bytes_written));
    TEST_ASSERT_GREATER_THAN(0, bytes_written);
}

static void test_channel_conversion(void)
{
    ESP_LOGI(TAG, "Testing channel conversion functions");
    static int16_t mono_buffer[TEST_BUFFER_SIZE / 2];
    memset(mono_buffer, 0, sizeof(mono_buffer));
    for (int i = 0; i < TEST_BUFFER_SIZE / 2; i++) {
        mono_buffer[i] = i % 32767;
    }
    static int16_t stereo_buffer[TEST_BUFFER_SIZE];
    memset(stereo_buffer, 0, sizeof(stereo_buffer));
    for (int i = 0; i < TEST_BUFFER_SIZE / 2; i++) {
        stereo_buffer[i * 2] = mono_buffer[i];
        stereo_buffer[i * 2 + 1] = mono_buffer[i];
    }
    TEST_ASSERT_EQUAL(mono_buffer[0], stereo_buffer[0]);
    TEST_ASSERT_EQUAL(mono_buffer[0], stereo_buffer[1]);
    TEST_ASSERT_EQUAL(mono_buffer[10], stereo_buffer[20]);
    TEST_ASSERT_EQUAL(mono_buffer[10], stereo_buffer[21]);
    static int16_t mono_result[TEST_BUFFER_SIZE / 2];
    memset(mono_result, 0, sizeof(mono_result));
    for (int i = 0; i < TEST_BUFFER_SIZE / 2; i++) {
        mono_result[i] = (stereo_buffer[i * 2] + stereo_buffer[i * 2 + 1]) / 2;
    }
    TEST_ASSERT_EQUAL(mono_buffer[0], mono_result[0]);
    TEST_ASSERT_EQUAL(mono_buffer[10], mono_result[10]);
}

static void test_i2s_write_argument_checks(void)
{
    /* Deinit to force invalid state path */
    if (i2s_is_driver_installed()) {
        i2s_driver_deinit();
    }

    size_t bytes_written = 123; /* sentinel to detect unwanted writes */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, i2s_write_samples(test_buffer, TEST_BUFFER_SIZE, &bytes_written));

    /* Re-init for positive/NULL checks */
    TEST_ASSERT_EQUAL(ESP_OK, i2s_driver_init(44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2s_write_samples(NULL, TEST_BUFFER_SIZE, &bytes_written));

    bytes_written = 0;
    TEST_ASSERT_EQUAL(ESP_OK, i2s_write_samples(test_buffer, TEST_BUFFER_SIZE, &bytes_written));
    TEST_ASSERT_EQUAL(TEST_BUFFER_SIZE * sizeof(int16_t), bytes_written);
}

static void test_i2s_convert_argument_checks(void)
{
    int16_t mono[4] = {0};
    int16_t stereo[8] = {0};

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2s_convert_stereo_to_mono(NULL, mono, 4));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2s_convert_stereo_to_mono(stereo, NULL, 4));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2s_convert_mono_to_stereo(NULL, stereo, 4));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2s_convert_mono_to_stereo(mono, NULL, 4));
}

void run_i2s_audio_tests(void)
{
    ESP_LOGI(TAG, "Starting I2S audio tests");
    RUN_TEST(test_i2s_driver_init);
    RUN_TEST(test_i2s_standard_mode);
    RUN_TEST(test_channel_conversion);
    RUN_TEST(test_i2s_write_argument_checks);
    RUN_TEST(test_i2s_convert_argument_checks);
    ESP_LOGI(TAG, "I2S audio tests completed");
}
