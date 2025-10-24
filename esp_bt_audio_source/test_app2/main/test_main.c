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

// Declare external test function
extern void app_test_main(void);

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
    printf("--------------------------------------\n");
    
    ESP_LOGI(TAG, "-------- BLUETOOTH TEST SUMMARY --------");
    ESP_LOGI(TAG, "Tests run     : %d", Unity.NumberOfTests);
    ESP_LOGI(TAG, "Tests passed  : %d", Unity.NumberOfTests - Unity.TestFailures);
    ESP_LOGI(TAG, "Tests failed  : %d", Unity.TestFailures);
    ESP_LOGI(TAG, "--------------------------------------");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Bluetooth Audio Source Test Suite");
    
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
    
    // CRITICAL: Modify this log message that's causing confusion
    ESP_LOGI(TAG, "All tests completed. Test application will now enter idle loop.");
    
    // Display a clear marker that we're in idle loop so we can see it in the logs
    printf("\n\n*** ENTERING IDLE LOOP - TESTS COMPLETE ***\n\n");
    
    // IMPORTANT: Enter idle loop instead of restarting!
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        // Print a heartbeat every second to show we're still running
        printf(".");
        fflush(stdout);
    }
}
