#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "test_app_main.h"
#include "esp_system.h"  // For ESP_LOGI and related macros

static const char *TAG = "TEST_APP_MAIN";

// External test function declarations
extern void run_bt_pairing_tests(void);
extern void run_bt_a2dp_tests(void);
extern void run_audio_tests(void);

void app_test_main(void)
{
    ESP_LOGI(TAG, "Starting Bluetooth pairing tests");
    run_bt_pairing_tests();
    ESP_LOGI(TAG, "Bluetooth pairing tests completed");

    ESP_LOGI(TAG, "Starting Bluetooth A2DP tests");
    run_bt_a2dp_tests();
    ESP_LOGI(TAG, "Bluetooth A2DP tests completed");

#ifndef CONFIG_BT_MOCK_TESTING
    ESP_LOGI(TAG, "Starting audio processing tests");
    run_audio_tests();
    ESP_LOGI(TAG, "Audio processing tests completed");
#endif

    // REMOVE THIS LINE! It's causing the restart:
    // esp_restart();
    
    // Don't restart, let the main function handle the completion
}