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
extern void run_i2s_audio_tests(void); // Updated function that runs both I2S tests
extern void run_audio_pipeline_tests(void);
extern void run_bt_pairing_tests(void); // Added forward declaration for BT pairing tests

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Bluetooth Audio Source Test Suite");
    
    bt_init();
    bt_scan_stop();
    
    ESP_LOGI(TAG, "Bluetooth tests are DISABLED - focusing on I2S implementation");

    // Run I2S audio tests - now using the unified function
    run_i2s_audio_tests();
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Running audio buffer and pipeline tests");
    run_audio_pipeline_tests();

    // Add a delay before running BT tests
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Uncomment this line to run the BT pairing tests
    ESP_LOGI(TAG, "Running Bluetooth pairing tests");
    run_bt_pairing_tests();

    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "All tests completed");
    
    bt_scan_stop();  // Make sure any scan is stopped
    bt_mock_cleanup();  // Clean up mock resources
    
    ESP_LOGI(TAG, "All tests completed. Test application will now restart.");
    vTaskDelay(pdMS_TO_TICKS(500));
}