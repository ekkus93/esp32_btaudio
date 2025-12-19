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
extern void run_auto_tests(void);

void app_test_main(void)
{
    ESP_LOGI(TAG, "Starting auto-run of all compiled tests");
    run_auto_tests();
    ESP_LOGI(TAG, "Auto-run of tests completed");

    // REMOVE THIS LINE! It's causing the restart:
    // esp_restart();
    
    // Don't restart here; return to the caller so the Unity harness
    // (in `test_main.c`) can call UNITY_END() and print the summary.
    // Wait briefly to flush logs then return.
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "Tests complete — returning to test harness to finish summary");
    return;
}

void app_main_bt_pairing_tests(void)
{
    ESP_LOGI(TAG, "Running Bluetooth pairing test group");
    /* Forward to the auto-runner so pairing tests are included
     * in the unified on-device run. This avoids duplicate symbols
     * and keeps group-based entry points working. */
    run_auto_tests();
}

void app_main_bt_a2dp_tests(void)
{
    ESP_LOGI(TAG, "Running Bluetooth A2DP test group");
    /* Forward to the auto-runner so A2DP tests are included. */
    run_auto_tests();
}

void run_test_group(const char *test_group)
{
    if (test_group == NULL) {
        ESP_LOGW(TAG, "Requested test group is NULL; skipping");
        return;
    }

    if (strcmp(test_group, "bt_pairing") == 0) {
        app_main_bt_pairing_tests();
    } else if (strcmp(test_group, "bt_a2dp") == 0) {
        app_main_bt_a2dp_tests();
    } else {
        ESP_LOGW(TAG, "Unknown test group '%s'", test_group);
    }
}