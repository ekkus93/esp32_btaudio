#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "test_app_main.h"
#include "audio_test_main.h"
#include "i2s_audio_test.h"
#include "i2s_channel_test.h"
#include "i2s_test.h"
#include "pcm_format_test.h"
#include "audio_pipeline_test.h"
#include "audio_processor_test.h"

static const char *TAG = "TEST_APP_AUDIO";

typedef void (*audio_suite_fn_t)(void);

static void execute_suite(const char *name, audio_suite_fn_t fn, bool standalone)
{
    if (!fn) {
        ESP_LOGE(TAG, "Suite '%s' has no handler", name ? name : "<null>");
        return;
    }

    ESP_LOGI(TAG, "Running %s suite", name);

    if (standalone) {
        UNITY_BEGIN();
        fn();
        UNITY_END();
    } else {
        fn();
    }

    ESP_LOGI(TAG, "%s suite completed", name);
}

void app_test_main(void)
{
    ESP_LOGI(TAG, "Starting aggregated audio test suites");

    execute_suite("audio smoke", run_all_audio_tests, false);
    execute_suite("i2s audio", run_i2s_audio_tests, false);
    execute_suite("i2s channel", app_main_i2s_channel_tests, false);
    execute_suite("pcm format", app_main_pcm_format_tests, false);
    execute_suite("audio pipeline", run_audio_pipeline_tests, false);
    // execute_suite("audio processor", run_audio_processor_tests, false);  // Removed: old WAV playback tests

    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "Audio test suites completed");
}

void run_test_group(const char *test_group)
{
    if (test_group == NULL) {
        ESP_LOGW(TAG, "Requested test group is NULL; skipping");
        return;
    }
    if (strcmp(test_group, "audio_smoke") == 0) {
        execute_suite("audio_smoke", run_all_audio_tests, true);
    } else if (strcmp(test_group, "i2s_audio") == 0) {
        execute_suite("i2s_audio", run_i2s_audio_tests, true);
    } else if (strcmp(test_group, "i2s_channel") == 0) {
        execute_suite("i2s_channel", app_main_i2s_channel_tests, true);
    } else if (strcmp(test_group, "pcm_format") == 0) {
        execute_suite("pcm_format", app_main_pcm_format_tests, true);
    } else if (strcmp(test_group, "pipeline") == 0) {
        execute_suite("pipeline", run_audio_pipeline_tests, true);
    // } else if (strcmp(test_group, "audio_processor") == 0) {
    //     execute_suite("audio_processor", run_audio_processor_tests, true);  // Removed: old WAV playback tests
    } else {
        ESP_LOGW(TAG, "Unknown audio test group '%s'", test_group);
    }
}
