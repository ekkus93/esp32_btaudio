/**
 * @file bt_app_name.c
 * @brief Bluetooth device name management
 * 
 * This module provides functionality for setting and retrieving the Bluetooth device
 * name. It handles persistent storage of the device name in NVS (Non-Volatile Storage)
 * and manages Bluetooth stack restarts when the name is changed.
 */
#include "bluetooth/bt_app_name.h"
#include "bluetooth/bt_app_global.h"
#include "bluetooth/bt_app_init.h"
#include "custom_log.h"
#include "nvs_flash.h" // For NVS flash API functions
#include "nvs.h" // For NVS key-value storage functions

#define TAG "BT_APP_NAME"

/**
 * @brief Set the Bluetooth device name
 * 
 * Updates the device name in the Bluetooth stack and persists it to NVS.
 * After setting the name, it restarts the Bluetooth stack to apply the change.
 * 
 * @param name The new name for the Bluetooth device
 * @return ESP_OK on success, error code on failure
 */
esp_err_t bluetooth_set_device_name(const char *name) {
    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
        // Set the name in the Bluetooth stack
        esp_err_t ret = esp_bt_gap_set_device_name(name);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to set device name: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Save the device name to NVS for persistence across reboots
        nvs_handle_t nvs_handle;
        ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Store the name with the predefined key
        ret = nvs_set_str(nvs_handle, BT_DEVICE_NAME_KEY, name);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to save device name to NVS: %s", esp_err_to_name(ret));
            nvs_close(nvs_handle);
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Commit the change to ensure it's written
        ret = nvs_commit(nvs_handle);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to commit device name to NVS: %s", esp_err_to_name(ret));
            nvs_close(nvs_handle);
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        nvs_close(nvs_handle);
        xSemaphoreGive(s_bt_resource_mutex);
    }

    // Restart the Bluetooth stack to apply the new name
    return restart_bluetooth_stack();
}

/**
 * @brief Get the current Bluetooth device name
 * 
 * Retrieves the device name from NVS. If no name is found in NVS,
 * it uses the default name and stores it to NVS for future use.
 * 
 * @param name Buffer to store the retrieved name
 * @param max_len Maximum length of the name buffer
 * @return ESP_OK on success, error code on failure
 */
esp_err_t bluetooth_get_device_name(char *name, size_t max_len) {
    // Read the device name from NVS
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    // Get the name string, respecting buffer size limit
    size_t name_len = max_len;
    ret = nvs_get_str(nvs_handle, BT_DEVICE_NAME_KEY, name, &name_len);
    
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // If no name stored, use the default name and save it
        strncpy(name, DEFAULT_BT_DEVICE_NAME, max_len);
        name[max_len - 1] = '\0';  // Ensure null-termination
        
        // Save the default name to NVS
        ret = nvs_set_str(nvs_handle, BT_DEVICE_NAME_KEY, name);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to save default device name to NVS: %s", esp_err_to_name(ret));
        } else {
            // Commit the default name
            ret = nvs_commit(nvs_handle);
            if (ret != ESP_OK) {
                SAFE_ESP_LOGE(TAG, "Failed to commit default device name to NVS: %s", esp_err_to_name(ret));
            }
        }
    } else if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to read device name from NVS: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    SAFE_ESP_LOGI(TAG, "Device name: %s", name);
    return ret;
}