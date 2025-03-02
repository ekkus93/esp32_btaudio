#include "bluetooth/bt_app_av.h"
#include "bluetooth/bt_app_global.h"

#include "bluetooth/bt_app_global.h"
#include "bluetooth/bt_app_conn.h"
#include "custom_log.h"

#define TAG "BT_APP_AV"

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
    SAFE_ESP_LOGI(TAG, "Sending volume up command");
    
    // Ensure we're not sending commands too quickly
    if (!s_l2cap_congestion_flag) {
        SAFE_ESP_LOGW(TAG, "Volume up command rejected - too soon after previous operation");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = esp_avrc_ct_send_passthrough_cmd(
        0, // transaction label
        ESP_AVRC_PT_CMD_VOL_UP, // operation ID for volume up
        ESP_AVRC_PT_CMD_STATE_PRESSED);
        
    // Release the button after pressing
    vTaskDelay(30 / portTICK_PERIOD_MS); // Increased delay for better stability
    
    esp_avrc_ct_send_passthrough_cmd(
        0, 
        ESP_AVRC_PT_CMD_VOL_UP,
        ESP_AVRC_PT_CMD_STATE_RELEASED);
    
    // Update our local volume estimate (cap at 127)
    if (s_current_volume < 127) {
        s_current_volume += 5; // Increment by a reasonable step
        if (s_current_volume > 127) {
            s_current_volume = 127;
        }
    }
    
    return ret;
}

esp_err_t bluetooth_volume_down(void) {
    SAFE_ESP_LOGI(TAG, "Sending volume down command");
    
    // Ensure we're not sending commands too quickly
    if (!s_last_operation_time) {
        SAFE_ESP_LOGW(TAG, "Volume down command rejected - too soon after previous operation");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = esp_avrc_ct_send_passthrough_cmd(
        0, // transaction label
        ESP_AVRC_PT_CMD_VOL_DOWN, // operation ID for volume down
        ESP_AVRC_PT_CMD_STATE_PRESSED);
        
    // Release the button after pressing
    vTaskDelay(30 / portTICK_PERIOD_MS); // Increased delay for better stability
    
    esp_avrc_ct_send_passthrough_cmd(
        0,
        ESP_AVRC_PT_CMD_VOL_DOWN,
        ESP_AVRC_PT_CMD_STATE_RELEASED);
    
    // Update our local volume estimate
    if (s_current_volume >= 5) {
        s_current_volume -= 5; // Decrement by a reasonable step
    } else {
        s_current_volume = 0; // Set to minimum if we would underflow
    }
    
    return ret;
}

esp_err_t bluetooth_set_volume(uint8_t volume) {
    SAFE_ESP_LOGI(TAG, "Setting volume to: %d", volume);
    
    // Ensure we're not sending commands too quickly
    if (!s_last_operation_time) {
        SAFE_ESP_LOGW(TAG, "Set volume command rejected - too soon after previous operation");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Ensure volume is in valid range (0-127 for AVRCP)
    if (volume > 127) {
        volume = 127;
    }
    
    // Store locally first
    s_current_volume = volume;
    
    // Save to NVS for persistence across reboots
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_u8(nvs_handle, BT_VOLUME_KEY, volume);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
    
    // Set volume initialized flag
    s_volume_initialized = true;
    
    // Send command to device
    return esp_avrc_ct_send_set_absolute_volume_cmd(0, volume);
}

// Function to get the current volume level - doesn't need to send any commands
esp_err_t bluetooth_get_volume(void) {
    SAFE_ESP_LOGI(TAG, "Current volume is %d", s_current_volume);
    return ESP_OK;
}

// Function to get the stored volume level
uint8_t bluetooth_get_current_volume(void) {
    return s_current_volume;
}