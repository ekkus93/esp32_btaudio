#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "unity_fixture.h"
#include "test_app_main.h"
#include "esp_system.h"  // For ESP_LOGI and related macros

static const char *TAG = "TEST_APP_MAIN";

/* Forward declaration for the util_safe Unity fixture group runner */
extern void run_bt_pairing_tests(void);
extern void run_bt_a2dp_tests(void);
extern void run_command_interface_tests(void);

void app_test_main(void)
{
    ESP_LOGI(TAG, "Starting Bluetooth pairing tests");
    run_bt_pairing_tests();
    ESP_LOGI(TAG, "Bluetooth pairing tests completed");

    ESP_LOGI(TAG, "Starting Bluetooth A2DP tests");
    run_bt_a2dp_tests();
    ESP_LOGI(TAG, "Bluetooth A2DP tests completed");

    ESP_LOGI(TAG, "Starting command interface tests");
    run_command_interface_tests();
    ESP_LOGI(TAG, "Command interface tests completed");

    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "Tests complete — returning to test harness to finish summary");
    return;
}

void app_main_bt_pairing_tests(void)
{
    ESP_LOGI(TAG, "Running Bluetooth pairing test group");
    run_bt_pairing_tests();
}

void app_main_bt_a2dp_tests(void)
{
    ESP_LOGI(TAG, "Running Bluetooth A2DP test group");
    run_bt_a2dp_tests();
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
    } else if (strcmp(test_group, "command_interface") == 0) {
        ESP_LOGI(TAG, "Running command interface test group");
        run_command_interface_tests();
    } else {
        ESP_LOGW(TAG, "Unknown test group '%s'", test_group);
    }
}