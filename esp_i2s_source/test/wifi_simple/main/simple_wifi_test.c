/*
 * Minimal ESP32-S3 Wi-Fi hardware smoke test.
 *
 * This performs one blocking access-point scan. It intentionally does not
 * configure credentials, connect to an AP, request DHCP, initialize PSRAM, or
 * use any production esp_i2s_source component.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define MAX_AP_RECORDS 20

static const char *TAG = "WIFI_SCAN_TEST";

static void diag_step(const char *step)
{
    printf("DIAG|WIFI_SCAN|STEP|%s\n", step);
    fflush(stdout);
}

static void init_nvs_non_destructive(void)
{
    esp_err_t err = nvs_flash_init();

    if (err != ESP_OK) {
        printf("DIAG|WIFI_SCAN|FAIL|step=nvs_init,err=%s\n",
               esp_err_to_name(err));
        fflush(stdout);
        ESP_ERROR_CHECK(err);
    }
}

static void run_scan(void)
{
    wifi_ap_record_t records[MAX_AP_RECORDS];
    memset(records, 0, sizeof(records));

    uint16_t records_to_read = MAX_AP_RECORDS;
    uint16_t total_found = 0;

    ESP_LOGI(TAG, "Starting one blocking 2.4 GHz AP scan");

    ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, true));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&total_found));
    ESP_ERROR_CHECK(
        esp_wifi_scan_get_ap_records(&records_to_read, records));

    ESP_LOGI(TAG,
             "Scan complete: total=%u returned=%u",
             (unsigned)total_found,
             (unsigned)records_to_read);

    for (uint16_t i = 0; i < records_to_read; ++i) {
        ESP_LOGI(TAG,
                 "[%02u] ssid=\"%s\" rssi=%d channel=%u auth=%d",
                 (unsigned)i,
                 (const char *)records[i].ssid,
                 (int)records[i].rssi,
                 (unsigned)records[i].primary,
                 (int)records[i].authmode);
    }

    printf("DIAG|WIFI_SCAN|PASS|total=%u,returned=%u\n",
           (unsigned)total_found,
           (unsigned)records_to_read);
    fflush(stdout);
}

void app_main(void)
{
    printf("\nDIAG|WIFI_SCAN|APP_MAIN_ENTERED\n");
    fflush(stdout);

    init_nvs_non_destructive();
    diag_step("nvs_ok");

    ESP_ERROR_CHECK(esp_netif_init());
    diag_step("netif_ok");

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    diag_step("event_loop_ok");

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif != NULL);
    diag_step("sta_netif_ok");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    diag_step("wifi_init_ok");

    /*
     * Keep station configuration in RAM. This prevents credentials or station
     * settings left by another firmware image from becoming hidden test input.
     */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    diag_step("wifi_started");

    run_scan();

    /*
     * Returning from app_main is valid in ESP-IDF. The application task is
     * deleted after the result is printed.
     */
}