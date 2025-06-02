/**
 * PCM Format Tests
 * 
 * Tests for PCM format handling, bit depth conversion, endianness, etc.
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

static const char *PCM_FORMAT_TAG = "PCM_FORMAT_TEST";

#define TEST_BUFFER_SIZE 1024

// Create a simple buffer for testing PCM format conversion
static int16_t test_buffer_16bit[TEST_BUFFER_SIZE];
static uint8_t test_buffer_24bit[TEST_BUFFER_SIZE * 3]; // 3 bytes per 24-bit sample
static int16_t result_buffer_16bit[TEST_BUFFER_SIZE];

void test_pcm_16bit_format(void) {
    ESP_LOGI(PCM_FORMAT_TAG, "Testing 16-bit PCM format");
    
    // Initialize test buffer with sample data
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        test_buffer_16bit[i] = (i % 256) * 128; // Simple pattern
    }
    
    // Verify basic properties
    TEST_ASSERT_EQUAL_INT16(0, test_buffer_16bit[0]);
    TEST_ASSERT_EQUAL_INT16(128, test_buffer_16bit[1]);
    TEST_ASSERT_EQUAL_INT16(256, test_buffer_16bit[2]);
    
    // Calculate RMS
    float rms = calculate_rms(test_buffer_16bit, TEST_BUFFER_SIZE, 0, 1);
    
    // Verify RMS is reasonable
    TEST_ASSERT_GREATER_THAN(0.0f, rms);
    
    // Make sure we can modify the buffer
    test_buffer_16bit[0] = 12345;
    TEST_ASSERT_EQUAL_INT16(12345, test_buffer_16bit[0]);
}

void test_pcm_24bit_format(void) {
    ESP_LOGI(PCM_FORMAT_TAG, "Testing 24-bit PCM format");
    
    // Initialize 16-bit buffer
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        test_buffer_16bit[i] = (i % 256) * 128; // Simple pattern
    }
    
    // Convert 16-bit to 24-bit PCM
    test_convert_16bit_to_24bit(test_buffer_16bit, test_buffer_24bit, TEST_BUFFER_SIZE);
    
    // Verify first few bytes of 24-bit data
    // We can't directly check the values but we can check they're non-zero
    TEST_ASSERT_NOT_EQUAL(0, test_buffer_24bit[0] | test_buffer_24bit[1] | test_buffer_24bit[2]);
    
    // Convert back to 16-bit
    test_convert_24bit_to_16bit(test_buffer_24bit, result_buffer_16bit, TEST_BUFFER_SIZE);
    
    // Verify conversion was lossless (within a small error margin)
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        TEST_ASSERT_INT16_WITHIN(2, test_buffer_16bit[i], result_buffer_16bit[i]);
    }
}

void test_pcm_sample_scaling(void) {
    ESP_LOGI(PCM_FORMAT_TAG, "Testing PCM sample scaling");
    
    // Test sample scaling by using specific values
    int16_t samples_16bit[4] = {0, 16384, -16384, 32767};
    
    // Convert 16-bit to 24-bit
    uint8_t samples_24bit[12]; // 3 bytes per 24-bit sample
    test_convert_16bit_to_24bit(samples_16bit, samples_24bit, 4);
    
    // Convert back to 16-bit
    int16_t result_samples[4];
    test_convert_24bit_to_16bit(samples_24bit, result_samples, 4);
    
    // Verify conversion was lossless
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_INT16_WITHIN(2, samples_16bit[i], result_samples[i]);
    }
}

void test_pcm_bit_depth_conversion(void) {
    ESP_LOGI(PCM_FORMAT_TAG, "Testing PCM bit depth conversion");
    
    // Initialize test buffer with sine wave
    generate_test_tone(test_buffer_16bit, TEST_BUFFER_SIZE, 440.0f, 44100.0f, 16384);
    
    // Convert to 24-bit and back
    test_convert_16bit_to_24bit(test_buffer_16bit, test_buffer_24bit, TEST_BUFFER_SIZE);
    test_convert_24bit_to_16bit(test_buffer_24bit, result_buffer_16bit, TEST_BUFFER_SIZE);
    
    // Calculate RMS of original and result signals
    float original_rms = calculate_rms(test_buffer_16bit, TEST_BUFFER_SIZE, 0, 1);
    float result_rms = calculate_rms(result_buffer_16bit, TEST_BUFFER_SIZE, 0, 1);
    
    // Verify RMS values are similar
    TEST_ASSERT_FLOAT_WITHIN(0.001f, original_rms, result_rms);
    
    // Verify samples are similar
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        TEST_ASSERT_INT16_WITHIN(2, test_buffer_16bit[i], result_buffer_16bit[i]);
    }
}

void app_main_pcm_format_tests(void)
{
    ESP_LOGI(PCM_FORMAT_TAG, "Starting PCM format tests");
    
    UNITY_BEGIN();
    
    RUN_TEST(test_pcm_16bit_format);
    RUN_TEST(test_pcm_24bit_format);
    RUN_TEST(test_pcm_sample_scaling);
    RUN_TEST(test_pcm_bit_depth_conversion);
    
    UNITY_END();
    
    ESP_LOGI(PCM_FORMAT_TAG, "PCM format tests completed");
}
