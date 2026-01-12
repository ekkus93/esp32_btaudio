/* Manual SPIFFS mount/read smoke for the spiffs_fail test app.
 * Mounts partition labeled "spiffs" at /spiffs, attempts to open
 * /spiffs/worker_long_norm.wav, logs presence/size, then unmounts.
 * This helper is optional and can be invoked from app_main when needed.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_log.h"

static const char *TAG = "spiffs_test";

static void spiffs_test_task(void *arg)
{
    (void)arg;
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 5,
        .format_if_mount_failed = false
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (err=%s)", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    const char *fname = "/spiffs/worker_long_norm.wav";
    FILE *f = fopen(fname, "rb");
    if (!f) {
        ESP_LOGI(TAG, "File not found: %s", fname);
    } else {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fclose(f);
        if (size >= 0) {
            ESP_LOGI(TAG, "Found %s, size=%ld bytes", fname, size);
        } else {
            ESP_LOGW(TAG, "Could not determine size for %s", fname);
        }
    }

    esp_vfs_spiffs_unregister(conf.partition_label);
    ESP_LOGI(TAG, "SPIFFS unmounted");
    vTaskDelete(NULL);
}

void app_main_spiffs_test_register(void)
{
    /* Run the smoke test in a short-lived task so it does not block app_main. */
    xTaskCreate(spiffs_test_task, "spiffs_test", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
}
