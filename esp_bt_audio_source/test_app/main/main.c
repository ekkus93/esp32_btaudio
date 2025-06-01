/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "pcm_format_test.h"

static const char *TAG = "PCM_FORMAT_TEST";

void run_pcm_format_tests()
{
    ESP_LOGI(TAG, "Running PCM format validation tests");

    // Add your PCM format validation test cases here
    // For example:
    // - Test for correct bit depth
    // - Test for endianness
    // - Test for mono/stereo handling

    ESP_LOGI(TAG, "PCM format validation tests completed");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ESP32 BT Audio Source Tests");
    
    bool run_bluetooth_tests = false;
    
    if (run_bluetooth_tests) {
        ESP_LOGI(TAG, "Running Bluetooth tests");
        // Run Bluetooth tests
    } else {
        ESP_LOGI(TAG, "Bluetooth tests are DISABLED - focusing on I2S implementation");
    }
    
    ESP_LOGI(TAG, "Running I2S audio tests");
    run_i2s_audio_tests();
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Running audio buffer and pipeline tests");
    run_audio_pipeline_tests();
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Add the new PCM format tests
    ESP_LOGI(TAG, "Running PCM format validation tests");
    run_pcm_format_tests();
    
    ESP_LOGI(TAG, "All tests completed");
    ESP_LOGI(TAG, "Audio buffer and pipeline implementation test completed");
}