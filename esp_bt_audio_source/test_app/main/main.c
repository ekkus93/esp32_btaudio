/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

// Include test headers
#include "i2s_audio_test.h"
#include "audio_pipeline_test.h"
#include "pcm_format_test.h"
#include "i2s_channel_test.h"
#include "bt_a2dp_test.h" // Include Bluetooth A2DP test header
#include "command_interface_test.c" // Include UART command interface test header

static const char *TAG = "BT_AUDIO_TEST";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ESP32 BT Audio Source Tests");
    
    // Enable Bluetooth tests
    bool run_bluetooth_tests = true;
    
    if (run_bluetooth_tests) {
        ESP_LOGI(TAG, "Running Bluetooth tests");
        run_bt_a2dp_tests(); // Call the Bluetooth test function
        
        // Allow some time for Bluetooth operations to complete
        vTaskDelay(pdMS_TO_TICKS(1000));
    } else {
        ESP_LOGI(TAG, "Bluetooth tests are DISABLED - focusing on I2S implementation");
    }
    
    ESP_LOGI(TAG, "Running I2S audio tests");
    run_i2s_audio_tests();
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Running audio buffer and pipeline tests");
    run_audio_pipeline_tests();
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Running PCM format validation tests");
    run_pcm_format_tests();
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Running I2S channel configuration tests");
    run_i2s_channel_tests();
    
    ESP_LOGI(TAG, "Running UART command interface tests");
    run_command_interface_tests();
    
    ESP_LOGI(TAG, "All tests completed");
    ESP_LOGI(TAG, "Audio buffer and pipeline implementation test completed");
}