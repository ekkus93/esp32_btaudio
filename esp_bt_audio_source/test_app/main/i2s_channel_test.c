/**
 * I2S Channel Tests
 * 
 * Tests for I2S channel configuration, mono/stereo handling, etc.
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

static const char *I2S_CHANNEL_TAG = "I2S_CHANNEL_TEST";

#define TEST_BUFFER_SIZE 1024

// Create buffers for testing channel conversion
static int16_t stereo_buffer[TEST_BUFFER_SIZE * 2]; // Stereo interleaved (L,R pairs)
static int16_t mono_buffer[TEST_BUFFER_SIZE];       // Mono (single channel)
static int16_t result_buffer[TEST_BUFFER_SIZE * 2]; // For conversion results

void test_stereo_buffer_format(void) {
    ESP_LOGI(I2S_CHANNEL_TAG, "Testing stereo buffer format");
    
    // Generate stereo test tone with different frequencies for left and right
    generate_stereo_test_tone(stereo_buffer, TEST_BUFFER_SIZE, 440.0f, 880.0f, 44100.0f, 16384);
    
    // Verify first few samples of stereo data
    // Left channel should be different from right channel
    TEST_ASSERT_NOT_EQUAL(stereo_buffer[0], stereo_buffer[1]); // L != R
    TEST_ASSERT_NOT_EQUAL(stereo_buffer[2], stereo_buffer[3]); // L != R
    
    // Calculate RMS for left and right channels separately
    float left_rms = calculate_rms(stereo_buffer, TEST_BUFFER_SIZE * 2, 0, 2);  // Left channel
    float right_rms = calculate_rms(stereo_buffer, TEST_BUFFER_SIZE * 2, 1, 2); // Right channel
    
    // Both channels should have similar RMS (using same amplitude)
    TEST_ASSERT_FLOAT_WITHIN(0.01f, left_rms, right_rms);
    
    // Both channels should have non-zero RMS
    TEST_ASSERT_GREATER_THAN(0.0f, left_rms);
    TEST_ASSERT_GREATER_THAN(0.0f, right_rms);
}

void test_mono_buffer_format(void) {
    ESP_LOGI(I2S_CHANNEL_TAG, "Testing mono buffer format");
    
    // Generate mono test tone
    generate_test_tone(mono_buffer, TEST_BUFFER_SIZE, 440.0f, 44100.0f, 16384);
    
    // Verify first few samples
    TEST_ASSERT_NOT_EQUAL(0, mono_buffer[0] | mono_buffer[1] | mono_buffer[2]);
    
    // Calculate RMS
    float rms = calculate_rms(mono_buffer, TEST_BUFFER_SIZE, 0, 1);
    
    // Should have non-zero RMS
    TEST_ASSERT_GREATER_THAN(0.0f, rms);
}

void test_stereo_to_mono_conversion(void) {
    ESP_LOGI(I2S_CHANNEL_TAG, "Testing stereo to mono conversion");
    
    // Generate stereo test tone with identical frequencies for left and right
    generate_stereo_test_tone(stereo_buffer, TEST_BUFFER_SIZE, 440.0f, 440.0f, 44100.0f, 16384);
    
    // Convert stereo to mono
    test_convert_stereo_to_mono(stereo_buffer, mono_buffer, TEST_BUFFER_SIZE);
    
    // Check first few samples - should be average of stereo pairs
    for (int i = 0; i < 10; i++) {
        int32_t expected = ((int32_t)stereo_buffer[i*2] + (int32_t)stereo_buffer[i*2+1]) / 2;
        TEST_ASSERT_INT16_WITHIN(1, expected, mono_buffer[i]);
    }
    
    // Calculate RMS of stereo channels and mono
    float left_rms = calculate_rms(stereo_buffer, TEST_BUFFER_SIZE * 2, 0, 2);
    float right_rms = calculate_rms(stereo_buffer, TEST_BUFFER_SIZE * 2, 1, 2);
    float mono_rms = calculate_rms(mono_buffer, TEST_BUFFER_SIZE, 0, 1);
    
    // Mono RMS should be similar to average of stereo channel RMS values
    float avg_stereo_rms = (left_rms + right_rms) / 2.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.1f, avg_stereo_rms, mono_rms);
}

void test_mono_to_stereo_conversion(void) {
    ESP_LOGI(I2S_CHANNEL_TAG, "Testing mono to stereo conversion");
    
    // Generate mono test tone
    generate_test_tone(mono_buffer, TEST_BUFFER_SIZE/2, 440.0f, 44100.0f, 16384);
    
    // Convert mono to stereo
    test_convert_mono_to_stereo(mono_buffer, result_buffer, TEST_BUFFER_SIZE/2);
    
    // Check first few stereo pairs - should have identical L and R
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_INT16(result_buffer[i*2], result_buffer[i*2+1]);
        TEST_ASSERT_EQUAL_INT16(mono_buffer[i], result_buffer[i*2]);
    }
    
    // Calculate RMS of mono and resulting stereo channels
    float mono_rms = calculate_rms(mono_buffer, TEST_BUFFER_SIZE/2, 0, 1);
    float left_rms = calculate_rms(result_buffer, TEST_BUFFER_SIZE, 0, 2);
    float right_rms = calculate_rms(result_buffer, TEST_BUFFER_SIZE, 1, 2);
    
    // Stereo channels should have identical RMS
    TEST_ASSERT_EQUAL_FLOAT(left_rms, right_rms);
    
    // Mono RMS should equal stereo channel RMS
    TEST_ASSERT_EQUAL_FLOAT(mono_rms, left_rms);
}

void test_stereo_mono_round_trip(void) {
    ESP_LOGI(I2S_CHANNEL_TAG, "Testing stereo->mono->stereo round trip");
    
    // Generate stereo test tone with identical left and right
    generate_stereo_test_tone(stereo_buffer, TEST_BUFFER_SIZE, 440.0f, 440.0f, 44100.0f, 16384);
    
    // Convert stereo to mono
    test_convert_stereo_to_mono(stereo_buffer, mono_buffer, TEST_BUFFER_SIZE);
    
    // Convert mono back to stereo
    test_convert_mono_to_stereo(mono_buffer, result_buffer, TEST_BUFFER_SIZE);
    
    // Compare original stereo (with identical L/R) to round-trip result
    // They should be nearly identical for identical L/R input
    for (int i = 0; i < TEST_BUFFER_SIZE * 2; i++) {
        TEST_ASSERT_INT16_WITHIN(1, stereo_buffer[i], result_buffer[i]);
    }
}

void app_main_i2s_channel_tests(void)
{
    ESP_LOGI(I2S_CHANNEL_TAG, "Starting I2S channel tests");
    
    UNITY_BEGIN();
    
    RUN_TEST(test_stereo_buffer_format);
    RUN_TEST(test_mono_buffer_format);
    RUN_TEST(test_stereo_to_mono_conversion);
    RUN_TEST(test_mono_to_stereo_conversion);
    RUN_TEST(test_stereo_mono_round_trip);
    
    UNITY_END();
    
    ESP_LOGI(I2S_CHANNEL_TAG, "I2S channel tests completed");
}
