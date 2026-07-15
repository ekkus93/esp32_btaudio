#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "unity.h"

static const char *TAG = "DEVICE_TEST";

extern void device_test_main(void);

void app_main(void)
{
    ESP_LOGI(TAG, "esp_i2s_source device test suite");
    ESP_LOGI(TAG, "free_heap=%u SPIRAM=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    printf("\n\n----- UNITY TEST START -----\n");
    UNITY_BEGIN();
    device_test_main();
    int result = UNITY_END();

    printf("--- SUMMARY ---\n");
    printf("----- UNITY TEST COMPLETE: %s -----\n", result ? "FAIL" : "PASS");
    printf("Tests run    : %d\n", Unity.NumberOfTests);
    printf("Tests passed : %d\n", Unity.NumberOfTests - Unity.TestFailures);
    printf("Tests failed : %d\n", Unity.TestFailures);
    printf("TEST_RUN_COMPLETE: %d %d %d\n",
           Unity.NumberOfTests, Unity.TestFailures, 0);
    fflush(stdout);

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        printf(".");
        fflush(stdout);
    }
}
