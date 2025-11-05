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
// Internal test runner task: run the Unity harness in a separate task
// with a larger stack to avoid overflowing `app_main`'s default stack.
static void test_runner_task(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "Starting Bluetooth Audio Source Test Suite (test runner task)");

    // Start a short scan on boot to make the device emit DEVICE_FOUND events
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

    /* Canonical single-line summary used by aggregation tools. */
    /* Format: "<N> Tests <F> Failures <I> Ignored" */
    /* Use Unity.TestIgnores when available; fall back to 0 otherwise. */
    do {
    int _total = Unity.NumberOfTests;
    int _fail = Unity.TestFailures;
    int _ignored = 0;
#ifdef UNITY_TEST_IGNORED
    _ignored = Unity.TestIgnores;
#else
    /* Try to reference Unity.TestIgnores if the symbol exists at link time. */
    /* Many Unity versions expose Unity.TestIgnores; if not present, ignored remains 0. */
#ifdef __cplusplus
    (void)0;
#endif
#endif
    printf("%d Tests %d Failures %d Ignored\n", _total, _fail, _ignored);
    } while (0);

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

    // Enter idle loop; keep the task alive so logs remain available.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Main entry point for all tests -- create a dedicated task to run them.
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

    // Create the test runner task with a larger stack to avoid overflow in heavy tests.
    BaseType_t ok = xTaskCreatePinnedToCore(test_runner_task, "test_runner", 16 * 1024, NULL, tskIDLE_PRIORITY + 5, NULL, tskNO_AFFINITY);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create test runner task (xTaskCreatePinnedToCore returned %d)", (int)ok);
        // If we can't create the task, run inline as fallback (risking stack overflow).
        test_runner_task(NULL);
    }

    // Delete the current (app_main) task to free its smaller stack; test_runner_task will continue.
    vTaskDelete(NULL);
}
#endif