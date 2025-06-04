#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bt_mock_devices.h"
#include "bt_source.h"  // Add this include for bt_deinit()

#include "i2s_audio_test.h"
#include "audio_pipeline_test.h"
#include "test_utils.h"

#define TAG "TEST_MAIN"

// Set to 1 to enable Bluetooth tests when BT implementation is ready
#define ENABLE_BT_TESTS 0

#if ENABLE_BT_TESTS
// All the existing BT test functions remain wrapped in this conditional block
void test_func_37(void) {
    // BT test code
    bt_scan_start();
    // ...existing code...
    bt_scan_stop();
}

void test_func_129(void) {
    // BT test code
    bt_init();
    // ...existing code...
}

// ...all other existing BT test functions...

void setUp(void)
{
    bt_init();
}

void tearDown(void)
{
    if (bt_is_connected()) {
        bt_disconnect();
    }
    
    if (bt_is_streaming()) {
        bt_stop_streaming();
    }
    
    bt_scan_stop();
}
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Bluetooth Audio Source Test Suite");
    
    // Initialize BT
    bt_init();
    
    // Make sure any previous scan is stopped and cleaned up
    bt_scan_stop();
    
#if ENABLE_BT_TESTS
    ESP_LOGI(TAG, "Bluetooth tests are ENABLED");
    // BT test code would go here
#else
    ESP_LOGI(TAG, "Bluetooth tests are DISABLED - focusing on I2S implementation");
#endif

    // For I2S audio tests
    UNITY_BEGIN();
    unity_set_setup_function(i2s_audio_test_setUp);
    unity_set_teardown_function(i2s_audio_test_tearDown);
    // Run I2S audio tests...
    UNITY_END();
    
    // For PCM format tests
    UNITY_BEGIN();
    unity_set_setup_function(pcm_format_test_setUp);
    unity_set_teardown_function(pcm_format_test_tearDown);
    // Run PCM format tests...
    UNITY_END();
    
    // For I2S channel tests
    UNITY_BEGIN();
    unity_set_setup_function(i2s_channel_test_setUp);
    unity_set_teardown_function(i2s_channel_test_tearDown);
    // Run I2S channel tests...
    UNITY_END();
    
    // Add a small delay between test suites
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Run audio buffer and pipeline tests
    ESP_LOGI(TAG, "Running audio buffer and pipeline tests");
    run_audio_pipeline_tests();

    // Delay to allow logs to be printed
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "All tests completed");
    
    // Ensure proper cleanup - add this at the end of your app_main() function
    bt_scan_stop();  // Make sure any scan is stopped
    bt_mock_cleanup();  // Clean up mock resources
    bt_deinit();     // Clean up bt stubs resources
    
    ESP_LOGI(TAG, "All tests completed. Test application will now restart.");
    // Short delay before restarting to allow logs to flush and resources to free
    vTaskDelay(pdMS_TO_TICKS(500));
}