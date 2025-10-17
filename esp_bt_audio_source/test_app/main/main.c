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
#include "test_app_main.h"

static const char *TAG = "TEST_MAIN";

// Forward declarations for BT tests - these are implemented
extern void app_main_bt_pairing_tests(void);
extern void app_main_bt_a2dp_tests(void);


#ifndef APP_MAIN_DEFINED
#define APP_MAIN_DEFINED
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
    
    // Add a delay between test sets to keep logs readable
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "All tests completed. Test application will now restart.");
    esp_restart();
}
#endif