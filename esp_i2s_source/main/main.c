/*
 * esp_i2s_source — ESP32-S3 internet-radio / tone source that streams audio
 * over I2S to the ESP32-WROOM32 A2DP bridge. See docs/SPEC.md.
 *
 * INFRA-1: minimal bootstrap. app_main only boots the platform, inits NVS
 * once, and prints a boot diagnostic. All real work lives in components.
 */
#include <stdio.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_idf_version.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif

static const char *TAG = "main";

/* Initialise NVS exactly once (root CLAUDE.md contract mirrors esp_bt_audio_source). */
static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    init_nvs();

    size_t psram_bytes = 0;
#if CONFIG_SPIRAM
    psram_bytes = esp_psram_get_size();
#endif

    ESP_LOGI(TAG, "esp_i2s_source boot: idf=%s psram=%u KB free_heap=%u B",
             esp_get_idf_version(), (unsigned)(psram_bytes / 1024),
             (unsigned)esp_get_free_heap_size());

    /* Machine-parseable boot beacon (INFRA-1a acceptance). */
    printf("DIAG|BOOT|READY|psram_kb=%u,heap=%u\n",
           (unsigned)(psram_bytes / 1024), (unsigned)esp_get_free_heap_size());
}
