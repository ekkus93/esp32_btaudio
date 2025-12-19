#include <stdio.h>
#include "esp_log.h"
#include "unity.h"
#include "i2s_test.h"

static const char *TAG = "AUDIO_TEST";

void run_all_audio_tests(void)
{
    ESP_LOGI(TAG, "Running audio smoke tests");
    run_i2s_tests();
    ESP_LOGI(TAG, "Audio smoke tests completed");
}
