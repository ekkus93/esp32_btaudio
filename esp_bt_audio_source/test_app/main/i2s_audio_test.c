/**
 * I2S Audio Tests
 */

#include <stdio.h>
#include "unity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" // Add FreeRTOS for task delays
#include "freertos/task.h"     // Add for vTaskDelay
#include "i2s_audio.h"
#include "audio_test_helpers.h"
#include <string.h>

static const char *TAG = "I2S_AUDIO_TEST";

// Test data
#define TEST_BUFFER_SIZE 1024
static int16_t test_buffer[TEST_BUFFER_SIZE];

void i2s_audio_test_setUp(void) {
    // Initialize test data before each test
    memset(test_buffer, 0, sizeof(test_buffer));
}

void i2s_audio_test_tearDown(void) {
    // Clean up after each test
    i2s_driver_uninstall(I2S_NUM_0);  // Clean up I2S driver
}

// Test #23: I2S driver initialization
void test_i2s_driver_init(void) {
    ESP_LOGI(TAG, "Testing I2S driver initialization");
    
    // Initialize I2S driver using real component
    esp_err_t ret = i2s_driver_init(44100, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_FMT_RIGHT_LEFT);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check driver is installed
    TEST_ASSERT_TRUE(i2s_is_driver_installed());
    
    // Verify configuration
    i2s_config_t config;
    TEST_ASSERT_EQUAL(ESP_OK, i2s_get_config(&config));
    TEST_ASSERT_EQUAL(44100, config.sample_rate);
    TEST_ASSERT_EQUAL(I2S_BITS_PER_SAMPLE_16BIT, config.bits_per_sample);
    TEST_ASSERT_EQUAL(I2S_CHANNEL_FMT_RIGHT_LEFT, config.channel_format);
}

// Test #24: I2S standard mode configuration
void test_i2s_standard_mode(void) {
    ESP_LOGI(TAG, "Testing I2S standard mode configuration");
    
    // Configure I2S in standard mode using real component
    esp_err_t ret = i2s_configure_standard_mode();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify standard settings
    i2s_config_t config;
    TEST_ASSERT_EQUAL(ESP_OK, i2s_get_config(&config));
    
    // Standard mode should have:
    // - I2S Philips communication format 
    // - 16-bit samples
    // - 44.1kHz sample rate (common for audio)
    TEST_ASSERT_EQUAL(I2S_COMM_FORMAT_STAND_I2S, config.communication_format);
    TEST_ASSERT_EQUAL(I2S_BITS_PER_SAMPLE_16BIT, config.bits_per_sample);
    TEST_ASSERT_EQUAL(44100, config.sample_rate);
    
    // Test basic read/write operations
    generate_test_tone(test_buffer, TEST_BUFFER_SIZE, 1000.0f, 16000.0f, 44100);
    
    // Write samples to I2S
    size_t bytes_written = 0;
    TEST_ASSERT_EQUAL(ESP_OK, i2s_write_samples(test_buffer, TEST_BUFFER_SIZE, &bytes_written));
    TEST_ASSERT_GREATER_THAN(0, bytes_written);
}

// Main entry point for I2S Audio tests
void app_main_i2s_audio_tests(void) {
    ESP_LOGI(TAG, "Starting I2S audio tests");
    
    UNITY_BEGIN();
    
    // Run tests
    RUN_TEST(test_i2s_driver_init);
    RUN_TEST(test_i2s_standard_mode);
    
    UNITY_END();
    
    ESP_LOGI(TAG, "I2S audio tests completed");
}
