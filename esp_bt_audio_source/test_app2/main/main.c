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
#include "unity.h"
#include "test_app_main.h"
#include "bt_source.h"

static const char *TAG = "TEST_MAIN";

// Forward declaration for the consolidated auto-run harness
extern void app_test_main(void);


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

    // Start a short scan on boot to make the device emit DEVICE_FOUND events
    // This helps capture events during host stress runs when an interactive
    // monitor isn't used. Keep the scan short to avoid altering test timing.
    esp_err_t scan_ret = bt_scan(10); // 10 second scan
    if (scan_ret == ESP_OK) {
        ESP_LOGI(TAG, "Started boot scan (10s)");
    } else {
        ESP_LOGW(TAG, "Boot scan call returned: %d", scan_ret);
    }
    
    printf("\n\n----- UNITY TEST START -----\n");
    UNITY_BEGIN();

    // Run the consolidated auto-test harness once; it drives every
    // compiled Unity test via RUN_TEST and defers to component mocks.
    app_test_main();

    // Close out the Unity session so the standard footer is emitted.
    int result = UNITY_END();
    printf("--- SUMMARY ---\n");
    printf("----- UNITY TEST COMPLETE: %s -----\n", result ? "FAIL" : "PASS");
    printf("-------- BLUETOOTH TEST SUMMARY --------\n");
    printf("Tests run    : %d\n", Unity.NumberOfTests);
    printf("Tests passed : %d\n", Unity.NumberOfTests - Unity.TestFailures);
    printf("Tests failed : %d\n", Unity.TestFailures);
    printf("--------------------------------------\n");

    ESP_LOGI(TAG, "-------- BLUETOOTH TEST SUMMARY --------");
    ESP_LOGI(TAG, "Tests run     : %d", Unity.NumberOfTests);
    ESP_LOGI(TAG, "Tests passed  : %d", Unity.NumberOfTests - Unity.TestFailures);
    ESP_LOGI(TAG, "Tests failed  : %d", Unity.TestFailures);
    ESP_LOGI(TAG, "--------------------------------------");

    printf("======== OVERALL TEST SUMMARY ========\n");
    printf("Tests run    : %d\n", Unity.NumberOfTests);
    printf("Tests passed : %d\n", Unity.NumberOfTests - Unity.TestFailures);
    printf("Tests failed : %d\n", Unity.TestFailures);
    printf("Success rate : %.1f%%\n", (Unity.NumberOfTests > 0) ?
           ((Unity.NumberOfTests - Unity.TestFailures) * 100.0f / Unity.NumberOfTests) : 0.0f);
    printf("=====================================\n");

    ESP_LOGI(TAG, "======== OVERALL TEST SUMMARY ========");
    ESP_LOGI(TAG, "Tests run     : %d", Unity.NumberOfTests);
    ESP_LOGI(TAG, "Tests passed  : %d", Unity.NumberOfTests - Unity.TestFailures);
    ESP_LOGI(TAG, "Tests failed  : %d", Unity.TestFailures);
    ESP_LOGI(TAG, "Success rate  : %.1f%%",
             (Unity.NumberOfTests > 0) ?
             ((Unity.NumberOfTests - Unity.TestFailures) * 100.0f / Unity.NumberOfTests) : 0.0f);
    ESP_LOGI(TAG, "=====================================");

    ESP_LOGI(TAG, "All tests completed. Test application will now enter idle loop.");
    printf("\n\n*** ENTERING IDLE LOOP - TESTS COMPLETE ***\n\n");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif