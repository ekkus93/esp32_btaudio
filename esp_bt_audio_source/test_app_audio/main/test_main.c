#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "unity.h"
#include "test_app_main.h"

static const char *TAG = "TEST_MAIN_AUDIO";
extern void app_test_main(void);

static void run_audio_tests(void)
{
    ESP_LOGI(TAG, "========== STARTING AUDIO TESTS ==========");
    printf("\n\n----- UNITY TEST START -----\n");
    UNITY_BEGIN();
    app_test_main();
    int result = UNITY_END();
    printf("--- SUMMARY ---\n");
    printf("----- UNITY TEST COMPLETE: %s -----\n", result ? "FAIL" : "PASS");
    printf("-------- AUDIO TEST SUMMARY --------\n");
    printf("Tests run    : %d\n", Unity.NumberOfTests);
    printf("Tests passed : %d\n", Unity.NumberOfTests - Unity.TestFailures);
    printf("Tests failed : %d\n", Unity.TestFailures);
    printf("--------------------------------------\n");
    ESP_LOGI(TAG, "-------- AUDIO TEST SUMMARY --------");
    ESP_LOGI(TAG, "Tests run     : %d", Unity.NumberOfTests);
    ESP_LOGI(TAG, "Tests passed  : %d", Unity.NumberOfTests - Unity.TestFailures);
    ESP_LOGI(TAG, "Tests failed  : %d", Unity.TestFailures);
    ESP_LOGI(TAG, "--------------------------------------");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting audio-focused Unity test suite");
    run_audio_tests();
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
    ESP_LOGI(TAG, "All tests completed. Test application will now enter idle loop.");
    printf("\n\n*** ENTERING IDLE LOOP - TESTS COMPLETE ***\n\n");
    /* Print a deterministic, machine-parseable final summary line so
       test runners that rely on a numeric summary can detect completion.
       Format matches the runner's expected regex: "<N> Tests <F> Failures <I> Ignored" */
    printf("%d Tests %d Failures %d Ignored\n", Unity.NumberOfTests, Unity.TestFailures, 0);
    /* Also emit a clear token for humans/diagnostics */
    printf("TEST_RUN_COMPLETE: %d %d %d\n", Unity.NumberOfTests, Unity.TestFailures, 0);
    fflush(stdout);
    /* Give the UART a moment to drain before entering the idle loop */
    vTaskDelay(200 / portTICK_PERIOD_MS);

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        printf(".");
        fflush(stdout);
    }
}
