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
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "unity.h"

// Test function declarations
void app_main_bt_a2dp_tests(void);
void app_main_bt_pairing_tests(void);
void app_main_i2s_audio_tests(void);
void app_main_audio_pipeline_tests(void);
void app_main_pcm_format_tests(void);
void app_main_i2s_channel_tests(void);

static const char *TAG = "BT_AUDIO_TEST";

// Main entry point for all tests
void app_main(void)
{
    // Initialize NVS for storage
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting ESP32 BT Audio Source Tests");
    
    // Run all test suites
    ESP_LOGI(TAG, "Running Bluetooth tests");
    app_main_bt_a2dp_tests();
    
    ESP_LOGI(TAG, "Running Bluetooth pairing tests");
    app_main_bt_pairing_tests();
    
    ESP_LOGI(TAG, "Running I2S audio tests");
    app_main_i2s_audio_tests();
    
    ESP_LOGI(TAG, "Running audio buffer and pipeline tests");
    app_main_audio_pipeline_tests();
    
    ESP_LOGI(TAG, "Running PCM format validation tests");
    app_main_pcm_format_tests();
    
    ESP_LOGI(TAG, "Running I2S channel configuration tests");
    app_main_i2s_channel_tests();
    
    ESP_LOGI(TAG, "All tests completed");
}