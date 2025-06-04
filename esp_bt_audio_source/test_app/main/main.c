/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/**
 * ESP32 Bluetooth Audio Source Test Application
 * 
 * Main entry point for running all tests.
 */

#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_bt.h"

static const char *TAG = "TEST_MAIN";

// Forward declarations for BT tests - these are implemented
extern void app_main_bt_pairing_tests(void);
extern void app_main_bt_a2dp_tests(void);

// Forward declarations for audio tests - will be stubbed
#ifndef CONFIG_BT_MOCK_TESTING
extern void app_main_i2s_audio_tests(void);
extern void app_main_audio_pipeline_tests(void);
extern void app_main_pcm_format_tests(void);
extern void app_main_i2s_channel_tests(void);
#else
// Stub implementations when only testing BT functionality
void app_main_i2s_audio_tests(void) {
    ESP_LOGI(TAG, "I2S audio tests skipped in BT mock testing mode");
}

void app_main_audio_pipeline_tests(void) {
    ESP_LOGI(TAG, "Audio pipeline tests skipped in BT mock testing mode");
}

void app_main_pcm_format_tests(void) {
    ESP_LOGI(TAG, "PCM format tests skipped in BT mock testing mode");
}

void app_main_i2s_channel_tests(void) {
    ESP_LOGI(TAG, "I2S channel tests skipped in BT mock testing mode");
}
#endif

// Main entry point for all tests
void app_main(void)
{
    // Initialize NVS for storage
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "Erasing NVS flash");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "Starting Bluetooth Audio Source Test Suite");
    
    // Run BT pairing tests
    app_main_bt_pairing_tests();
    
    // Run BT A2DP tests
    app_main_bt_a2dp_tests();
    
#ifndef CONFIG_BT_MOCK_TESTING
    // Only run audio tests when not in BT mock testing mode
    // Add a delay between test sets
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Run I2S audio tests
    app_main_i2s_audio_tests();
    
    // Run audio pipeline tests
    app_main_audio_pipeline_tests();
    
    // Run PCM format tests
    app_main_pcm_format_tests();
    
    // Run I2S channel tests
    app_main_i2s_channel_tests();
#else
    ESP_LOGI(TAG, "Audio tests skipped in BT mock testing mode");
#endif
    
    ESP_LOGI(TAG, "All tests completed. Test application will now restart.");
    esp_restart();
}