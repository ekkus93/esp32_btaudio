/**
 * @file ping_util.c
 * @brief Implementation of PING functionality for network connectivity testing
 * 
 * This module provides functions to ping remote hosts over the network to verify
 * connectivity and measure response times. It uses ESP-IDF's ping_sock API.
 */
#include "ping_utils.h"
#include "esp_log.h"
#include "ping/ping_sock.h"
#include "esp_wifi.h"
#include "lwip/inet.h"  // Include this for ipaddr_aton
#include "custom_log.h"

#define TAG "PING"

/**
 * @brief Callback function that processes ping operation results
 * 
 * This function is called by the ping API to report statistics about
 * the ping operation, including packets transmitted, received, and total time.
 * It's invoked for successful pings, timeouts, and when the ping operation ends.
 *
 * @param hdl The ping session handle
 * @param args Optional user-provided arguments (unused in this implementation)
 */
static void ping_results(esp_ping_handle_t hdl, void *args) {
    uint32_t transmitted = 0, received = 0, total_time_ms = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
    SAFE_ESP_LOGI(TAG, "%lu packets transmitted, %lu received, time %lums", (unsigned long)transmitted, (unsigned long)received, (unsigned long)total_time_ms);
}

/**
 * @brief Ping a specified host with a given number of ICMP packets
 * 
 * This function first verifies that Wi-Fi is connected, then attempts to
 * resolve and ping the specified host address. The ping operation is performed
 * asynchronously, with results reported through the ping_results callback.
 *
 * @param host IP address of the target host to ping
 * @param count Number of ICMP echo requests to send
 * @return ESP_OK if ping operation was started successfully
 *         ESP_ERR_WIFI_NOT_CONNECT if Wi-Fi is not connected
 *         ESP_ERR_INVALID_ARG if host IP address is invalid
 *         Other error codes if ping session creation fails
 */
esp_err_t ping_host(const char *host, int count) {
    // Check Wi-Fi connection status
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Wi-Fi is not connected");
        return ESP_ERR_WIFI_NOT_CONNECT;
    }

    // Parse the IP address string to an IP address structure
    ip_addr_t target_addr;
    if (ipaddr_aton(host, &target_addr) == 0) {
        SAFE_ESP_LOGE(TAG, "Invalid IP address: %s", host);
        return ESP_ERR_INVALID_ARG;
    }

    // Configure the ping session with default settings and our specific parameters
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.target_addr = target_addr;
    ping_config.count = count;

    // Set up callback functions for ping events
    esp_ping_callbacks_t cbs = {
        .cb_args = NULL,
        .on_ping_success = ping_results,
        .on_ping_timeout = ping_results,
        .on_ping_end = ping_results,
    };

    // Create a new ping session with our configuration
    esp_ping_handle_t ping;
    esp_err_t err = esp_ping_new_session(&ping_config, &cbs, &ping);
    if (err != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to create ping session: %s", esp_err_to_name(err));
        return err;
    }

    // Start the ping operation asynchronously
    esp_ping_start(ping);
    return ESP_OK;
}
