/*
 * Minimal, safe implementation of the test configuration helpers.
 *
 * Purpose: provide a permissive runtime filter so tests are not skipped
 * unintentionally while we investigate why some declared tests don't
 * appear in the canonical logs. This implementation keeps state in
 * a small static struct and defaults to allowing all tests to run.
 */

#include "test_config.h"
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "test_config";

/* Internal copy of test configuration used at runtime. */
static test_config_t s_test_config = {
    .use_mock_bt = false,
    .skip_audio_tests = false,
    .run_bt_tests = true,
    .test_duration = 30,
    .test_device_name = { 0 },
};

esp_err_t test_config_init(test_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Initialize provided struct to sensible defaults */
    memcpy(config, &s_test_config, sizeof(s_test_config));
    ESP_LOGI(TAG, "test_config initialized (defaults)");
    return ESP_OK;
}

esp_err_t test_config_set(const test_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(&s_test_config, config, sizeof(s_test_config));
    ESP_LOGI(TAG, "test_config updated");
    return ESP_OK;
}

esp_err_t test_config_get(test_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(config, &s_test_config, sizeof(s_test_config));
    return ESP_OK;
}

bool test_config_should_run_test(const char *test_name)
{
    /*
     * Minimal permissive behavior: allow all tests to run.
     * This avoids runtime skipping while debugging test-count mismatches.
     * If needed, this can be extended to check s_test_config flags
     * (for example skip audio tests when the name matches). For now
     * returning true is the safest option to ensure all tests execute.
     */
    (void)test_name; /* unused */
    return true;
}

const char* test_config_get_device_name(void)
{
    if (s_test_config.test_device_name[0] == '\0') {
        return NULL;
    }
    return s_test_config.test_device_name;
}
