#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bt_mock_devices.h"
#include "bt_source.h"
#include "unity.h"

static const char *TAG = "TEST_APP_MAIN";

extern void bt_mock_cleanup(void);

// Forward declarations for the test functions
extern void i2s_audio_test_setUp(void);
extern void i2s_audio_test_tearDown(void);
extern void pcm_format_test_setUp(void);
extern void pcm_format_test_tearDown(void);
extern void i2s_channel_test_setUp(void);
extern void i2s_channel_test_tearDown(void);
extern void run_audio_pipeline_tests(void);

// Explicitly declare the I2S test functions that are defined in i2s_audio_test.c
extern void test_i2s_driver_init(void);
extern void test_i2s_standard_mode(void);

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Bluetooth Audio Source Test Suite");
    
    bt_init();
    bt_scan_stop();
    
    ESP_LOGI(TAG, "Bluetooth tests are DISABLED - focusing on I2S implementation");

    UNITY_BEGIN();
    // Run I2S audio tests with manual setup and teardown
    i2s_audio_test_setUp();
    test_i2s_driver_init();
    test_i2s_standard_mode();
    i2s_audio_test_tearDown();
    UNITY_END();
    
    // Comment out tests that aren't ready yet
    /* 
    UNITY_BEGIN();
    // Run PCM format tests with manual setup and teardown
    pcm_format_test_setUp();
    // PCM test functions would be called here
    pcm_format_test_tearDown();
    UNITY_END();
    
    UNITY_BEGIN();
    // Run I2S channel tests with manual setup and teardown
    i2s_channel_test_setUp();
    // I2S channel test functions would be called here
    i2s_channel_test_tearDown();
    UNITY_END();
    */
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Running audio buffer and pipeline tests");
    run_audio_pipeline_tests();

    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "All tests completed");
    
    bt_scan_stop();  // Make sure any scan is stopped
    bt_mock_cleanup();  // Clean up mock resources
    
    ESP_LOGI(TAG, "All tests completed. Test application will now restart.");
    vTaskDelay(pdMS_TO_TICKS(500));
}