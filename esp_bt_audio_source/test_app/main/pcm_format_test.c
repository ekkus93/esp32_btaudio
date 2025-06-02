/**
 * PCM Format Tests
 * 
 * Tests for PCM format handling, bit depth conversion, endianness, etc.
 */

#include <stdio.h>
#include "unity.h"
#include "esp_log.h"
#include "audio_test_helpers.h"
#include "pcm_processing.h"  // Actual component header
#include <string.h>

static const char *TAG = "PCM_FORMAT_TEST";

// Test buffers
#define TEST_BUFFER_SIZE 512
static int16_t test_buffer_16bit[TEST_BUFFER_SIZE];
static int32_t test_buffer_24bit[TEST_BUFFER_SIZE];
static int16_t result_buffer_16bit[TEST_BUFFER_SIZE];

void pcm_format_test_setUp(void) {
    // Initialize test data before each test
    memset(test_buffer_16bit, 0, sizeof(test_buffer_16bit));
    memset(test_buffer_24bit, 0, sizeof(test_buffer_24bit));
    memset(result_buffer_16bit, 0, sizeof(result_buffer_16bit));
}

void pcm_format_test_tearDown(void) {
    // Clean up after each test
}

// Test #36: 16-bit PCM format handling
void test_pcm_16bit_format(void) {
    ESP_LOGI(TAG, "Testing 16-bit PCM format handling");
    
    // Generate test tone in 16-bit PCM
    generate_test_tone(test_buffer_16bit, TEST_BUFFER_SIZE, 1000.0f, 16000.0f, 44100);
    
    // Verify the generated audio has expected characteristics
    float rms = calculate_rms(test_buffer_16bit, TEST_BUFFER_SIZE);
    ESP_LOGI(TAG, "Generated 16-bit tone RMS: %.2f", rms);
    
    // Test with real pcm_processing component
    TEST_ASSERT_EQUAL(ESP_OK, pcm_process_16bit(test_buffer_16bit, TEST_BUFFER_SIZE));
    
    // Verify basic properties of 16-bit PCM data
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        TEST_ASSERT_LESS_OR_EQUAL_INT32(32767, test_buffer_16bit[i]);
        TEST_ASSERT_GREATER_OR_EQUAL_INT32(-32768, test_buffer_16bit[i]);
    }
}

// Test #37: 24-bit PCM format handling
void test_pcm_24bit_format(void) {
    ESP_LOGI(TAG, "Testing 24-bit PCM format handling");
    
    // Generate test tone in 16-bit PCM
    generate_test_tone(test_buffer_16bit, TEST_BUFFER_SIZE, 1000.0f, 16000.0f, 44100);
    
    // Convert to 24-bit
    convert_16bit_to_24bit(test_buffer_16bit, test_buffer_24bit, TEST_BUFFER_SIZE);
    
    // Test with real pcm_processing component
    TEST_ASSERT_EQUAL(ESP_OK, pcm_process_24bit(test_buffer_24bit, TEST_BUFFER_SIZE));
    
    // Convert back to 16-bit for verification
    convert_24bit_to_16bit(test_buffer_24bit, result_buffer_16bit, TEST_BUFFER_SIZE);
    
    // Should be similar to the original with some tolerance for rounding errors
    TEST_ASSERT_TRUE(compare_audio_buffers(test_buffer_16bit, result_buffer_16bit, 
                                          TEST_BUFFER_SIZE, 0.01f));
}

// Test #38: PCM endianness conversion
void test_pcm_endianness(void) {
    ESP_LOGI(TAG, "Testing PCM endianness conversion");
    
    // Generate test data with known pattern
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        test_buffer_16bit[i] = (int16_t)(i * 100); // Arbitrary pattern
    }
    
    // Create copy of the original data for later comparison
    int16_t original_data[TEST_BUFFER_SIZE];
    memcpy(original_data, test_buffer_16bit, sizeof(original_data));
    
    // Convert to big endian using real component
    TEST_ASSERT_EQUAL(ESP_OK, pcm_convert_to_big_endian(test_buffer_16bit, TEST_BUFFER_SIZE));
    
    // Data should now be different (on little-endian architectures like ESP32)
    bool all_same = true;
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        if (original_data[i] != test_buffer_16bit[i]) {
            all_same = false;
            break;
        }
    }
    TEST_ASSERT_FALSE(all_same);
    
    // Convert back to little endian
    TEST_ASSERT_EQUAL(ESP_OK, pcm_convert_to_little_endian(test_buffer_16bit, TEST_BUFFER_SIZE));
    
    // Should match the original data again
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        TEST_ASSERT_EQUAL_INT16(original_data[i], test_buffer_16bit[i]);
    }
}

// Test #39: Bit depth conversion
void test_pcm_bit_depth_conversion(void) {
    ESP_LOGI(TAG, "Testing bit depth conversion");
    
    // Generate test tone in 16-bit PCM
    generate_test_tone(test_buffer_16bit, TEST_BUFFER_SIZE, 1000.0f, 16000.0f, 44100);
    
    // Convert 16-bit to 24-bit using real component
    TEST_ASSERT_EQUAL(ESP_OK, pcm_convert_16bit_to_24bit(test_buffer_16bit, test_buffer_24bit, TEST_BUFFER_SIZE));
    
    // Convert back for verification
    TEST_ASSERT_EQUAL(ESP_OK, pcm_convert_24bit_to_16bit(test_buffer_24bit, result_buffer_16bit, TEST_BUFFER_SIZE));
    
    // Compare RMS of original and converted-back data (should be very close)
    float original_rms = calculate_rms(test_buffer_16bit, TEST_BUFFER_SIZE);
    float result_rms = calculate_rms(result_buffer_16bit, TEST_BUFFER_SIZE);
    
    ESP_LOGI(TAG, "Original RMS: %.2f, Result RMS: %.2f", original_rms, result_rms);
    
    // Allow for small rounding errors in conversion
    TEST_ASSERT_FLOAT_WITHIN(1.0f, original_rms, result_rms);
}

// Main entry point for PCM format tests
void app_main_pcm_format_tests(void) {
    ESP_LOGI(TAG, "Starting PCM format tests");
    
    UNITY_BEGIN();
    
    // Run tests
    RUN_TEST(test_pcm_16bit_format);
    RUN_TEST(test_pcm_24bit_format);
    RUN_TEST(test_pcm_endianness);
    RUN_TEST(test_pcm_bit_depth_conversion);
    
    UNITY_END();
    
    ESP_LOGI(TAG, "PCM format tests completed");
}
