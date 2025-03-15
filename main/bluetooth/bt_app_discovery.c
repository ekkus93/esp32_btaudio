/**
 * @file bt_app_discovery.c
 * @brief Bluetooth device discovery functionality
 * 
 * This module provides functions to initiate and manage Bluetooth device discovery
 * (scanning). It includes both standard discovery and a "safe" discovery
 * method that ensures no active connections interfere with the scanning process.
 */
#include "bluetooth/bt_app_discovery.h"
#include "bluetooth/bt_app_global.h"
#include "bluetooth/bt_app_conn.h"
#include "custom_log.h"

#define TAG "BT_APP_DISCOVERY"

/**
 * @brief Start Bluetooth device discovery process
 * 
 * Initiates the Bluetooth discovery process to scan for nearby devices.
 * Uses the Bluetooth GAP API to scan with general inquiry mode.
 * Results are reported via the GAP event handler callback.
 *
 * @return ESP_OK if discovery was started successfully, error code otherwise
 */
esp_err_t bluetooth_start_discovery(void) {
    SAFE_ESP_LOGI(TAG, "Starting Bluetooth device discovery");
    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
        esp_err_t ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 30, 0);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to start discovery: %s", esp_err_to_name(ret));
        }
        xSemaphoreGive(s_bt_resource_mutex);
    }
    return ESP_OK;
}

/**
 * @brief Safely start Bluetooth device discovery
 * 
 * Checks if any active Bluetooth connections exist before starting discovery.
 * If a connection is active, it disconnects it first to ensure the discovery
 * process has full access to the Bluetooth radio hardware.
 * 
 * This helps prevent resource conflicts between discovery and connection handling.
 *
 * @return ESP_OK if discovery was started successfully, error code otherwise
 */
esp_err_t bluetooth_safe_start_discovery(void) {
    // First attempt to gracefully disconnect if connected
    if (s_a2d_state == APP_AV_STATE_CONNECTED || s_a2d_state == APP_AV_STATE_CONNECTING) {
        esp_err_t ret = bluetooth_disconnect_device();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            SAFE_ESP_LOGE(TAG, "Failed to disconnect: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    // Now we're guaranteed to be disconnected thanks to the mutex
    return bluetooth_start_discovery();
}
