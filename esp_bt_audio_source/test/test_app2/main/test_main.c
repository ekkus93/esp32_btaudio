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

// Unity test suite is fairly stack hungry; mirror test_app runner layout
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

static void run_bluetooth_tests(void)
{
    ESP_LOGI(TAG, "========== STARTING BLUETOOTH TESTS ==========");

    printf("\n\n----- UNITY TEST START -----\n");
    UNITY_BEGIN();

    app_test_main();

    int result = UNITY_END();
    printf("--- SUMMARY ---\n");
    printf("----- UNITY TEST COMPLETE: %s -----\n", result ? "FAIL" : "PASS");

    printf("-------- BLUETOOTH TEST SUMMARY --------\n");
    printf("%d Tests %d Failures %d Ignored\n", Unity.NumberOfTests, Unity.TestFailures, Unity.TestIgnores);
    printf("Tests run    : %d\n", Unity.NumberOfTests);
    printf("Tests passed : %d\n", Unity.NumberOfTests - Unity.TestFailures);
    printf("Tests failed : %d\n", Unity.TestFailures);
    printf("--------------------------------------\n");

    ESP_LOGI(TAG, "-------- BLUETOOTH TEST SUMMARY --------");
    ESP_LOGI(TAG, "Unity summary   : %d Tests %d Failures %d Ignored",
             Unity.NumberOfTests, Unity.TestFailures, Unity.TestIgnores);
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

    run_bluetooth_tests();

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

    UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "Unity runner stack high-water mark: %lu words (%lu bytes)",
             (unsigned long)watermark,
             (unsigned long)(watermark * sizeof(StackType_t)));

    ESP_LOGI(TAG, "All tests completed. Test application will now enter idle loop.");

    printf("\n\n*** ENTERING IDLE LOOP - TESTS COMPLETE ***\n\n");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
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
