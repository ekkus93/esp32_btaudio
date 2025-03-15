/**
 * @file bt_app_av.c
 * @brief Bluetooth Audio/Video control implementation
 * 
 * This module handles Bluetooth A2DP (Advanced Audio Distribution Profile) audio 
 * control operations including volume management and Bluetooth stack event handling.
 * It also provides initialization for A/V components and defines state strings for
 * debugging purposes. This file manages AVRCP (Audio/Video Remote Control Profile)
 * volume commands for controlling audio playback on connected devices.
 */
#include "bluetooth/bt_app_av.h"
#include "bluetooth/bt_app_audio.h" // For audio operation timing functions
#include "bluetooth/bt_app_global.h" // For global state variables
#include "bluetooth/bt_app_gap.h"
#include "bluetooth/bt_app_core.h"
#include "esp_avrc_api.h" // For AVRCP API functions
#include "nvs_flash.h" // For persisting volume settings
#include "nvs.h" // For NVS operations
#include "custom_log.h"
#include "freertos/queue.h" // For FreeRTOS queue handling
#include "esp_log.h"
#include "esp_a2dp_api.h"

#define TAG "BT_APP_AV"

/**
 * @brief Handles Bluetooth stack events
 * 
 * This callback function is triggered on Bluetooth stack initialization and other
 * stack-level events. It handles device name setting, GAP callback registration,
 * and configuring discoverable/connectable modes to prepare for Bluetooth operations.
 *
 * @param event Event type from the Bluetooth stack
 * @param p_param Event parameters (event-specific data)
 */
void bt_av_hdl_stack_evt(uint16_t event, void *p_param) {
    SAFE_ESP_LOGD(TAG, "%s event: %d", __func__, event);

    switch (event) {
        case BT_APP_STACK_UP_EVT: {
            // Set device name
            esp_bt_gap_set_device_name("ESP_A2DP_SRC");

            // Register GAP callback for device discovery and authentication events
            esp_bt_gap_register_callback(gap_event_handler);

            // Make device discoverable and connectable
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            break;
        }
        default:
            SAFE_ESP_LOGE(TAG, "%s unhandled event: %d", __func__, event);
            break;
    }
}

/**
 * @brief Increases the Bluetooth audio volume
 * 
 * Increments the volume by VOLUME_STEP if not already at maximum (127).
 * Uses AVRCP to send the volume command to the connected Bluetooth device,
 * ensuring the volume change takes effect on the remote device.
 *
 * @return ESP_OK on success, ESP_FAIL if not connected or other failure
 */
esp_err_t bluetooth_volume_up(void) {
    SAFE_ESP_LOGI(TAG, "###Increasing volume");
    // Check if connected to a device
    if (s_a2d_state != APP_AV_STATE_CONNECTED) {
        SAFE_ESP_LOGE(TAG, "Not connected to a device");
        return ESP_FAIL;
    }
    
    // Increase volume if not at maximum
    if (s_current_volume < 127) {
        s_current_volume += VOLUME_STEP;
        // Ensure volume doesn't exceed maximum
        if (s_current_volume > 127) s_current_volume = 127;
        SAFE_ESP_LOGI(TAG, "Setting volume to %d", s_current_volume);
        // Send AVRCP command to set volume on remote device
        esp_avrc_ct_send_set_absolute_volume_cmd(0, s_current_volume);
    } else {
        SAFE_ESP_LOGI(TAG, "Volume is already at maximum");
    }
    return ESP_OK;
}

/**
 * @brief Decreases the Bluetooth audio volume
 * 
 * Decrements the volume by VOLUME_STEP if not already at minimum (0).
 * Uses AVRCP to send the volume command to the connected device,
 * ensuring consistent volume level between local and remote devices.
 *
 * @return ESP_OK on success, ESP_FAIL if not connected or other failure
 */
esp_err_t bluetooth_volume_down(void) {
    SAFE_ESP_LOGI(TAG, "###Decreasing volume");
    // Check if connected to a device
    if (s_a2d_state != APP_AV_STATE_CONNECTED) {
        SAFE_ESP_LOGE(TAG, "Not connected to a device");
        return ESP_FAIL;
    }
    
    // Decrease volume if not at minimum
    if (s_current_volume > 0) {
        s_current_volume -= VOLUME_STEP;
        SAFE_ESP_LOGI(TAG, "Setting volume to %d", s_current_volume);
        // Send AVRCP command to set volume on remote device
        esp_avrc_ct_send_set_absolute_volume_cmd(0, s_current_volume);
    } else {
        SAFE_ESP_LOGI(TAG, "Volume is already at minimum");
    }
    return ESP_OK;
}

/**
 * @brief Sets the Bluetooth audio volume to a specific level
 * 
 * Sets the volume to the exact specified level (between 0-127).
 * Uses AVRCP to send the volume command to the connected device,
 * providing direct control over the audio volume.
 *
 * @param volume Target volume level (0-127)
 * @return ESP_OK on success, ESP_FAIL if not connected or other failure
 */
esp_err_t bluetooth_set_volume(uint8_t volume) {
    SAFE_ESP_LOGI(TAG, "###Setting volume to %d", volume);
    // Check if connected to a device
    if (s_a2d_state != APP_AV_STATE_CONNECTED) {
        SAFE_ESP_LOGE(TAG, "Not connected to a device");
        return ESP_FAIL;
    }
    
    // Store the new volume level
    s_current_volume = volume;
    SAFE_ESP_LOGI(TAG, "Setting volume to %d", s_current_volume);
    // Send AVRCP command to set absolute volume
    esp_avrc_ct_send_set_absolute_volume_cmd(0, s_current_volume);

    return ESP_OK;
}

/**
 * @brief Reports the current volume level in logs
 * 
 * Simply logs the current volume level without sending any commands.
 * Useful for debugging and status reporting.
 *
 * @return ESP_OK always
 */
esp_err_t bluetooth_get_volume(void) {
    SAFE_ESP_LOGI(TAG, "Current volume is %d", s_current_volume);
    return ESP_OK;
}

/**
 * @brief Returns the current volume level
 * 
 * Provides access to the current volume level for other modules.
 * This allows other components to adjust their behavior based on
 * the current audio volume level.
 *
 * @return Current volume value (0-127)
 */
uint8_t bluetooth_get_current_volume(void) {
    SAFE_ESP_LOGI(TAG, "###Getting current volume: %d", s_current_volume);
    return s_current_volume;
}