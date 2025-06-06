#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bt_source.h"
#include "unity.h"
#include "i2s_audio_test.h"   // Make sure this include is present
#include "audio_pipeline_test.h"
#include "bt_pairing_test.h"
#include "bt_a2dp_test.h" // Include for the A2DP tests

static const char *TAG = "TEST_APP_MAIN";

// Define a flag to control Bluetooth tests
// Set to true to enable Bluetooth tests
static const bool ENABLE_BT_TESTS = true;

void app_main(void) {
    ESP_LOGI(TAG, "Starting Bluetooth Audio Source Test Suite");
    
    // Initialize Bluetooth for testing
    esp_err_t ret = bt_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Bluetooth: %d", ret);
    }
    
    // Make sure scan is not active
    bt_scan_stop();
    
    if (ENABLE_BT_TESTS) {
        ESP_LOGI(TAG, "Bluetooth tests are ENABLED - running A2DP implementation tests");
        
        // Run all Bluetooth A2DP tests
        run_bt_a2dp_tests();
    } else {
        ESP_LOGI(TAG, "Bluetooth tests are DISABLED - focusing on I2S implementation");
    }
    
    // Run I2S audio tests - fix function name to match what's in i2s_audio_test.h
    ESP_LOGI(TAG, "Running I2S audio tests");
    run_i2s_audio_tests();
    vTaskDelay(pdMS_TO_TICKS(500)); // Small delay between test suites
    
    // Run audio buffer and pipeline tests
    ESP_LOGI(TAG, "Running audio buffer and pipeline tests");
    run_audio_pipeline_tests();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Small delay between test suites
    
    // Run Bluetooth pairing tests
    ESP_LOGI(TAG, "Running Bluetooth pairing tests");
    run_bt_pairing_tests();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Small delay between test suites
    
    // All tests completed
    ESP_LOGI(TAG, "All tests completed");
    
    bt_scan_stop(); // Ensure BT scan is stopped before restart
    ESP_LOGI(TAG, "All tests completed. Test application will now restart.");
    
    // Wait a bit before restarting
    vTaskDelay(pdMS_TO_TICKS(500));
}