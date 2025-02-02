#include "ping.h"
#include "esp_log.h"
#include "ping/ping_sock.h"
#include "esp_wifi.h"
#include "lwip/inet.h"  // Include this for ipaddr_aton

#define TAG "PING"

static void ping_results(esp_ping_handle_t hdl, void *args) {
    uint32_t transmitted = 0, received = 0, total_time_ms = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
    ESP_LOGI(TAG, "%lu packets transmitted, %lu received, time %lums", (unsigned long)transmitted, (unsigned long)received, (unsigned long)total_time_ms);
}

esp_err_t ping_host(const char *host, int count) {
    // Check Wi-Fi connection status
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi is not connected");
        return ESP_ERR_WIFI_NOT_CONNECT;
    }

    ip_addr_t target_addr;
    if (ipaddr_aton(host, &target_addr) == 0) {
        ESP_LOGE(TAG, "Invalid IP address: %s", host);
        return ESP_ERR_INVALID_ARG;
    }

    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.target_addr = target_addr;
    ping_config.count = count;

    esp_ping_callbacks_t cbs = {
        .cb_args = NULL,
        .on_ping_success = ping_results,
        .on_ping_timeout = ping_results,
        .on_ping_end = ping_results,
    };

    esp_ping_handle_t ping;
    esp_err_t err = esp_ping_new_session(&ping_config, &cbs, &ping);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ping session: %s", esp_err_to_name(err));
        return err;
    }

    esp_ping_start(ping);
    return ESP_OK;
}
