/**
 * I2S Audio Tests
 */

#include <stdio.h>
#include "unity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" 
#include "freertos/task.h"     
#include "i2s_audio_test.h" // Include the header for function declarations

// Use the old I2S API for now
#include "i2s_audio.h"
#include "audio_test_helpers.h"
#include <string.h>
#include "driver/i2s.h"  // This should include the declaration for i2s_driver_uninstall

static const char *TAG = "I2S_AUDIO_TEST";

// Test data
#define TEST_BUFFER_SIZE 1024
static int16_t test_buffer[TEST_BUFFER_SIZE];

/**
 * @brief Set up environment for each test
 */
void setUp(void) {
    ESP_LOGI(TAG, "Setting up I2S audio test");
    // Initialize test data before each test
    memset(test_buffer, 0, sizeof(test_buffer));
    
    // Make sure I2S driver is initialized at setup
    // This ensures we have a driver to uninstall during tearDown
    esp_err_t ret = i2s_driver_init(44100, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_FMT_RIGHT_LEFT);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "Failed to initialize I2S driver in setUp");
}

/**
 * @brief Clean up after each test
 */
void tearDown(void) {
    ESP_LOGI(TAG, "Tearing down I2S audio test");
    // Only attempt uninstall if driver is installed
    if (i2s_is_driver_installed()) {
        i2s_driver_uninstall(I2S_NUM_0);
    }
}

/**
 * @brief Test I2S driver initialization
 * 
 * Define as a normal function to match header declaration
 */
void test_i2s_driver_init(void) {
    ESP_LOGI(TAG, "Testing I2S driver initialization");
    
    // First ensure driver is uninstalled if previously installed
    if (i2s_is_driver_installed()) {
        i2s_driver_uninstall(I2S_NUM_0);
    }
    
    // Now initialize I2S driver using real component
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

/**
 * @brief Test I2S standard mode configuration
 * 
 * Define as a normal function to match header declaration
 */
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

/**
 * @brief Main entry point for I2S Audio tests
 */
void run_i2s_audio_tests(void) {
    ESP_LOGI(TAG, "Starting I2S audio tests");
    
    // Use Unity macros to properly register and run tests
    UNITY_BEGIN();
    RUN_TEST(test_i2s_driver_init);
    RUN_TEST(test_i2s_standard_mode);
    UNITY_END();
    
    ESP_LOGI(TAG, "I2S audio tests completed");
}
