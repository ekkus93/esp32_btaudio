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

// Add this forward declaration after the includes and defines
static void delayed_volume_task(void *param); // Forward declaration

// Define a queue for passing volume values to the task
static QueueHandle_t volume_queue = NULL;

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

// Generic volume handling for all devices
esp_err_t bluetooth_set_volume(uint8_t volume) {
    // Store the volume locally first
    s_current_volume = volume;
    
    // Save volume to NVS
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret == ESP_OK) {
        nvs_set_u8(nvs_handle, BT_VOLUME_KEY, volume);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
    
    // Ensure Bluetooth is ready and connected
    if (s_a2d_state != APP_AV_STATE_CONNECTED) {
        ESP_LOGW(TAG, "Cannot set volume: not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    SAFE_ESP_LOGI(TAG, "Setting volume to: %d", volume);
    
    // Use is_operation_time_ok with proper delay handling
    if (!is_operation_time_ok()) {
        ESP_LOGW(TAG, "Set volume command rejected - too soon after previous operation");
        
        // Schedule a delayed retry for any device
        ESP_LOGI(TAG, "Will retry volume change in 1 second");
        
        // Create the queue if it doesn't exist
        if (volume_queue == NULL) {
            volume_queue = xQueueCreate(1, sizeof(uint8_t));
        }
        
        // Send the volume value to the queue
        if (volume_queue != NULL) {
            if (xQueueSend(volume_queue, &volume, 0) == pdPASS) {
                if (xTaskCreate(delayed_volume_task, "vol_task", 2048, NULL, 1, NULL) != pdPASS) {
                    ESP_LOGE(TAG, "Failed to create delayed volume task");
                }
            } else {
                ESP_LOGE(TAG, "Failed to send volume to queue");
            }
        }
        return ESP_OK; // Indicate success since we'll retry
    }
    
    // Send volume command (0-127 range)
    return esp_avrc_ct_send_set_absolute_volume_cmd(0, volume);
}

// Add this helper function for delayed volume changes
static void delayed_volume_task(void *param) {
    uint8_t volume;
    
    // Wait 1 second before retry
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Receive the volume value from the queue
    if (volume_queue != NULL) {
        if (xQueueReceive(volume_queue, &volume, 0) == pdPASS) {
            // Retry the volume command
            ESP_LOGI(TAG, "Retrying volume change to %d", volume);
            esp_avrc_ct_send_set_absolute_volume_cmd(0, volume);
        }
    }
    
    vTaskDelete(NULL);
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