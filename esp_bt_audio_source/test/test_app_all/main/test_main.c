#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "unity.h"

// Include test headers
#include "test_app_main.h"

static const char *TAG = "TEST_MAIN";

// Unity test suite is fairly stack hungry (command parser, mock layers, large
// printf buffers). Running everything on the default main task stack (configured
// to 3584 bytes in the active sdkconfig) trips FreeRTOS stack overflow checks
// when the scan-heavy tests execute. Run the Unity harness from a dedicated
// task with an explicit stack budget so the main task remains minimal.
#define UNITY_TASK_STACK_BYTES   (16384)
#define UNITY_TASK_STACK_WORDS   (UNITY_TASK_STACK_BYTES / sizeof(StackType_t))
#define UNITY_TASK_PRIORITY      (tskIDLE_PRIORITY + 3)

// Declare external test function
extern void app_test_main(void);

static void unity_runner_task(void *arg);

static void init_nvs_or_die(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init returned %s, erasing and retrying", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

// Run all Bluetooth-related tests
void run_bluetooth_tests(void)
{
    ESP_LOGI(TAG, "========== STARTING BLUETOOTH TESTS ==========");
    
    // Start Unity test session with proper output
    printf("\n\n----- UNITY TEST START -----\n");
    UNITY_BEGIN();
    
    // Run the test application
    app_test_main();
    
    // End Unity test session and capture the result
    // The result will be printed by Unity itself
    int result = UNITY_END();
    // Emit a stable, machine-parseable marker used by capture tooling
    // (kept for backward-compatibility with CI/scripts that look for
    //  '--- SUMMARY ---')
    printf("--- SUMMARY ---\n");
    printf("----- UNITY TEST COMPLETE: %s -----\n", result ? "FAIL" : "PASS");
    
    // Print our own custom summary using Unity's global structure
    // Access the global Unity structure directly
    printf("-------- BLUETOOTH TEST SUMMARY --------\n");
    printf("Tests run    : %d\n", Unity.NumberOfTests);
    printf("Tests passed : %d\n", Unity.NumberOfTests - Unity.TestFailures);
    printf("Tests failed : %d\n", Unity.TestFailures);
    // NOTE(2025-10-27): `test_bluetooth_connection`, `test_connection_failure_handling`,
    // and `test_a2dp_paired_devices` remain red; keep this list in sync until we land fixes.
    printf("--------------------------------------\n");
    
    ESP_LOGI(TAG, "-------- BLUETOOTH TEST SUMMARY --------");
    ESP_LOGI(TAG, "Tests run     : %d", Unity.NumberOfTests);
    ESP_LOGI(TAG, "Tests passed  : %d", Unity.NumberOfTests - Unity.TestFailures);
    ESP_LOGI(TAG, "Tests failed  : %d", Unity.TestFailures);
    ESP_LOGI(TAG, "--------------------------------------");
}

static void unity_runner_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Starting Bluetooth Audio Source Test Suite");
    init_nvs_or_die();

    // Run tests
    run_bluetooth_tests();

    // Overall test summary
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
    ESP_LOGI(TAG, "Success rate  : %.1f%%", (Unity.NumberOfTests > 0) ?
             ((Unity.NumberOfTests - Unity.TestFailures) * 100.0f / Unity.NumberOfTests) : 0.0f);
    ESP_LOGI(TAG, "=====================================");
    
    // Capture remaining stack to validate the configured budget
    UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "Unity runner stack high-water mark: %lu words (%lu bytes)",
             (unsigned long)watermark,
             (unsigned long)(watermark * sizeof(StackType_t)));

    // CRITICAL: Modify this log message that's causing confusion
    ESP_LOGI(TAG, "All tests completed. Test application will now enter idle loop.");
    
    // Display a clear marker that we're in idle loop so we can see it in the logs
    printf("\n\n*** ENTERING IDLE LOOP - TESTS COMPLETE ***\n\n");
    
    // IMPORTANT: Enter idle loop instead of restarting!
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        // Print a heartbeat every second to show we're still running
        printf(".");
        fflush(stdout);
    }
}

void app_main(void)
{
    BaseType_t rc = xTaskCreate(unity_runner_task,
                                "unity_runner",
                                UNITY_TASK_STACK_WORDS,
                                NULL,
                                UNITY_TASK_PRIORITY,
                                NULL);
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Unity runner task; running inline on main stack");
        unity_runner_task(NULL);
    }
}
