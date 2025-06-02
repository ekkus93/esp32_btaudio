/**
 * I2S Channel Tests
 * 
 * Tests for I2S channel configuration, mono/stereo handling, etc.
 */

#include <stdio.h>
#include "unity.h"
#include "esp_log.h"
#include "audio_test_helpers.h"
#include "i2s_audio.h"  // Actual component header
#include <string.h>

static const char *TAG = "I2S_CHANNEL_TEST";

// Test data
#define TEST_BUFFER_SIZE 1024
static int16_t mono_buffer[TEST_BUFFER_SIZE];
static int16_t stereo_buffer[TEST_BUFFER_SIZE * 2];  // Twice as large for interleaved L/R
static int16_t result_buffer[TEST_BUFFER_SIZE * 2];

void i2s_channel_test_setUp(void) {
    // Initialize test data before each test
    memset(mono_buffer, 0, sizeof(mono_buffer));
    memset(stereo_buffer, 0, sizeof(stereo_buffer));
    memset(result_buffer, 0, sizeof(result_buffer));
}

void i2s_channel_test_tearDown(void) {
    // Clean up after each test
    i2s_driver_uninstall(I2S_NUM_0);  // Clean up I2S driver
}

// Test #40: Mono channel configuration
void test_mono_channel_config(void) {
    ESP_LOGI(TAG, "Testing mono channel configuration");
    
    // Configure I2S for mono using real component
    i2s_config_t config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,  // Mono
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1
    };
    
    // Initialize with mono configuration using real component
    esp_err_t ret = i2s_config_mono(config.sample_rate, config.bits_per_sample);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Get and verify channel format
    i2s_channel_fmt_t channel_fmt = i2s_get_channel_format();
    TEST_ASSERT_EQUAL(I2S_CHANNEL_FMT_ONLY_LEFT, channel_fmt);
    
    // Generate mono test tone
    generate_test_tone(mono_buffer, TEST_BUFFER_SIZE, 1000.0f, 16000.0f, 44100);
    
    // Write samples using real component - just need to confirm it works
    ret = i2s_write_mono_samples(mono_buffer, TEST_BUFFER_SIZE);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

// Test #41: Stereo channel configuration
void test_stereo_channel_config(void) {
    ESP_LOGI(TAG, "Testing stereo channel configuration");
    
    // Configure I2S for stereo using real component
    i2s_config_t config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  // Stereo
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1
    };
    
    // Initialize with stereo configuration using real component
    esp_err_t ret = i2s_config_stereo(config.sample_rate, config.bits_per_sample);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Get and verify channel format
    i2s_channel_fmt_t channel_fmt = i2s_get_channel_format();
    TEST_ASSERT_EQUAL(I2S_CHANNEL_FMT_RIGHT_LEFT, channel_fmt);
    
    // Generate stereo test tone with different frequencies for L/R
    generate_stereo_test_tone(stereo_buffer, TEST_BUFFER_SIZE/2, 
                             1000.0f, 1500.0f, 16000.0f, 44100);
    
    // Write stereo samples using real component
    ret = i2s_write_stereo_samples(stereo_buffer, TEST_BUFFER_SIZE/2);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

// Test #42: Stereo to mono conversion
void test_stereo_to_mono_conversion(void) {
    ESP_LOGI(TAG, "Testing stereo to mono conversion");
    
    // Generate stereo test tone with different frequencies for L/R
    generate_stereo_test_tone(stereo_buffer, TEST_BUFFER_SIZE/2, 
                             1000.0f, 1500.0f, 16000.0f, 44100);
    
    // Convert stereo to mono using real component
    TEST_ASSERT_EQUAL(ESP_OK, i2s_convert_stereo_to_mono(stereo_buffer, mono_buffer, TEST_BUFFER_SIZE/2));
    
    // Check RMS characteristics
    float stereo_left_rms = 0.0f;
    float stereo_right_rms = 0.0f;
    
    // Extract and calculate RMS for left and right channels separately
    int16_t left_channel[TEST_BUFFER_SIZE/2];
    int16_t right_channel[TEST_BUFFER_SIZE/2];
    
    for (int i = 0; i < TEST_BUFFER_SIZE/2; i++) {
        left_channel[i] = stereo_buffer[i*2];      // Left samples
        right_channel[i] = stereo_buffer[i*2+1];   // Right samples
    }
    
    stereo_left_rms = calculate_rms(left_channel, TEST_BUFFER_SIZE/2);
    stereo_right_rms = calculate_rms(right_channel, TEST_BUFFER_SIZE/2);
    float mono_rms = calculate_rms(mono_buffer, TEST_BUFFER_SIZE/2);
    
    ESP_LOGI(TAG, "Stereo L RMS: %.2f, Stereo R RMS: %.2f, Mono RMS: %.2f", 
            stereo_left_rms, stereo_right_rms, mono_rms);
    
    // The mono RMS should be approximately the average of left and right
    float expected_mono_rms = (stereo_left_rms + stereo_right_rms) / 2.0f;
    TEST_ASSERT_FLOAT_WITHIN(expected_mono_rms * 0.1f, expected_mono_rms, mono_rms);
}

// Test #43: Mono to stereo conversion
void test_mono_to_stereo_conversion(void) {
    ESP_LOGI(TAG, "Testing mono to stereo conversion");
    
    // Generate mono test tone
    generate_test_tone(mono_buffer, TEST_BUFFER_SIZE/2, 1000.0f, 16000.0f, 44100);
    
    // Convert mono to stereo using real component
    TEST_ASSERT_EQUAL(ESP_OK, i2s_convert_mono_to_stereo(mono_buffer, stereo_buffer, TEST_BUFFER_SIZE/2));
    
    // Check that stereo buffer has duplicated mono samples in both channels
    for (int i = 0; i < TEST_BUFFER_SIZE/2; i++) {
        TEST_ASSERT_EQUAL_INT16(mono_buffer[i], stereo_buffer[i*2]);     // Left channel
        TEST_ASSERT_EQUAL_INT16(mono_buffer[i], stereo_buffer[i*2+1]);   // Right channel
    }
    
    // Calculate and compare RMS 
    float mono_rms = calculate_rms(mono_buffer, TEST_BUFFER_SIZE/2);
    
    // Extract left and right channels to separate buffers for RMS calculation
    int16_t left_channel[TEST_BUFFER_SIZE/2];
    int16_t right_channel[TEST_BUFFER_SIZE/2];
    
    for (int i = 0; i < TEST_BUFFER_SIZE/2; i++) {
        left_channel[i] = stereo_buffer[i*2];      // Left samples
        right_channel[i] = stereo_buffer[i*2+1];   // Right samples
    }
    
    float left_rms = calculate_rms(left_channel, TEST_BUFFER_SIZE/2);
    float right_rms = calculate_rms(right_channel, TEST_BUFFER_SIZE/2);
    
    ESP_LOGI(TAG, "Mono RMS: %.2f, Stereo L RMS: %.2f, Stereo R RMS: %.2f", 
            mono_rms, left_rms, right_rms);
    
    // Both channels should match original mono RMS
    TEST_ASSERT_FLOAT_WITHIN(0.1f, mono_rms, left_rms);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, mono_rms, right_rms);
}

// Test #44: Channel independence
void test_channel_independence(void) {
    ESP_LOGI(TAG, "Testing channel independence");
    
    // Create stereo buffer with completely different frequencies in L/R channels
    generate_stereo_test_tone(stereo_buffer, TEST_BUFFER_SIZE/2, 
                             440.0f, 880.0f, 16000.0f, 44100);
    
    // Process through real component's channel processing
    TEST_ASSERT_EQUAL(ESP_OK, i2s_process_channels(stereo_buffer, TEST_BUFFER_SIZE));
    
    // Extract left and right channels to check independence
    int16_t left_channel[TEST_BUFFER_SIZE/2];
    int16_t right_channel[TEST_BUFFER_SIZE/2];
    
    for (int i = 0; i < TEST_BUFFER_SIZE/2; i++) {
        left_channel[i] = stereo_buffer[i*2];      // Left samples
        right_channel[i] = stereo_buffer[i*2+1];   // Right samples
    }
    
    // Calculate correlation between channels
    int64_t correlation = 0;
    for (int i = 0; i < TEST_BUFFER_SIZE/2; i++) {
        correlation += (int64_t)left_channel[i] * (int64_t)right_channel[i];
    }
    
    // Normalize correlation
    float left_rms = calculate_rms(left_channel, TEST_BUFFER_SIZE/2);
    float right_rms = calculate_rms(right_channel, TEST_BUFFER_SIZE/2);
    float normalized_correlation = (float)correlation / (left_rms * right_rms * TEST_BUFFER_SIZE/2);
    
    ESP_LOGI(TAG, "Channel correlation: %.4f", normalized_correlation);
    
    // For independent channels with different frequencies, correlation should be close to zero
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 0.0f, normalized_correlation);
}

// Main entry point for I2S channel tests
void app_main_i2s_channel_tests(void) {
    ESP_LOGI(TAG, "Starting I2S channel configuration tests");
    
    UNITY_BEGIN();
    
    // Run tests
    RUN_TEST(test_mono_channel_config);
    RUN_TEST(test_stereo_channel_config);
    RUN_TEST(test_stereo_to_mono_conversion);
    RUN_TEST(test_mono_to_stereo_conversion);
    RUN_TEST(test_channel_independence);
    
    UNITY_END();
    
    ESP_LOGI(TAG, "I2S channel configuration tests completed");
}
