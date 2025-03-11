#include "bluetooth/bt_app_av.h"
#include "bluetooth/bt_app_core.h"  // Include this header
#include "bluetooth/bt_app_audio.h" // Add this to get is_operation_time_ok()
#include "bluetooth/bt_app_conn.h" // Add this to get gap_event_handler
#include "bluetooth/bt_app_global.h" // Include this header for global variables
#include "esp_avrc_api.h" // Include this for AVRCP API
#include "nvs_flash.h" // Include this for NVS functions
#include "custom_log.h"
#include "freertos/queue.h" // Include this for FreeRTOS queue
#include "esp_log.h"
#include "esp_a2dp_api.h"

#define TAG "BT_APP_AV"

// Connection state strings
const char *s_a2d_conn_state_str[] = {"Disconnected", "Connecting", "Connected", "Disconnecting"};

// Audio state strings
const char *s_a2d_audio_state_str[] = {"Suspended", "Stopped", "Started"};

// Define the audio state variable
esp_a2d_audio_state_t s_audio_state = ESP_A2D_AUDIO_STATE_STOPPED;

// Add the bt_av_hdl_stack_evt function to handle the stack event
void bt_av_hdl_stack_evt(uint16_t event, void *p_param) {
    SAFE_ESP_LOGD(TAG, "%s event: %d", __func__, event);

    switch (event) {
        case BT_APP_STACK_UP_EVT: {
            // Set device name
            esp_bt_gap_set_device_name("ESP_A2DP_SRC");

            // Register GAP callback
            esp_bt_gap_register_callback(gap_event_handler);

            // Set scan mode
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            break;
        }
        default:
            SAFE_ESP_LOGE(TAG, "%s unhandled event: %d", __func__, event);
            break;
    }
}

// AVRCP volume control functions
esp_err_t bluetooth_volume_up(void) {
    SAFE_ESP_LOGI(TAG, "###Increasing volume");
    if (s_a2d_state != APP_AV_STATE_CONNECTED) {
        SAFE_ESP_LOGE(TAG, "Not connected to a device");
        return ESP_FAIL;
    }
    
    if (s_current_volume < 127) {
        s_current_volume += VOLUME_STEP;
        if (s_current_volume > 127) s_current_volume = 127;
        SAFE_ESP_LOGI(TAG, "Setting volume to %d", s_current_volume);
        esp_avrc_ct_send_set_absolute_volume_cmd(0, s_current_volume);
    } else {
        SAFE_ESP_LOGI(TAG, "Volume is already at maximum");
    }
    return ESP_OK;
}

esp_err_t bluetooth_volume_down(void) {
    SAFE_ESP_LOGI(TAG, "###Decreasing volume");
    if (s_a2d_state != APP_AV_STATE_CONNECTED) {
        SAFE_ESP_LOGE(TAG, "Not connected to a device");
        return ESP_FAIL;
    }
    
    if (s_current_volume > 0) {
        s_current_volume -= VOLUME_STEP;
        SAFE_ESP_LOGI(TAG, "Setting volume to %d", s_current_volume);
        esp_avrc_ct_send_set_absolute_volume_cmd(0, s_current_volume);
    } else {
        SAFE_ESP_LOGI(TAG, "Volume is already at minimum");
    }
    return ESP_OK;
}

// Generic volume handling for all devices
esp_err_t bluetooth_set_volume(uint8_t volume) {
    SAFE_ESP_LOGI(TAG, "###Setting volume to %d", volume);
    if (s_a2d_state != APP_AV_STATE_CONNECTED) {
        SAFE_ESP_LOGE(TAG, "Not connected to a device");
        return ESP_FAIL;
    }
    
    s_current_volume = volume;
    SAFE_ESP_LOGI(TAG, "Setting volume to %d", s_current_volume);
    esp_avrc_ct_send_set_absolute_volume_cmd(0, s_current_volume);

    return ESP_OK;
}

// Function to get the current volume level - doesn't need to send any commands
esp_err_t bluetooth_get_volume(void) {
    SAFE_ESP_LOGI(TAG, "Current volume is %d", s_current_volume);
    return ESP_OK;
}

// Function to get the stored volume level
uint8_t bluetooth_get_current_volume(void) {
    SAFE_ESP_LOGI(TAG, "###Getting current volume: %d", s_current_volume);
    return s_current_volume;
}