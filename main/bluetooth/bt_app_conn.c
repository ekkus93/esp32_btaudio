#include "bluetooth/bt_app_conn.h"
#include "bluetooth/bt_app_global.h"
#include "bluetooth/bt_app_init.h"
#include "bluetooth/bt_app_a2dp.h"
#include "bluetooth/bt_app_av.h"
#include "custom_log.h"

#define TAG "BT_APP_CONN"

// Create a new timer task to periodically check for memory issues
static void memory_monitor_task(void *arg) {
    while(1) {
        // Get free heap memory
        size_t free_heap = esp_get_free_heap_size();
        SAFE_ESP_LOGI(TAG, "Free heap: %u bytes", free_heap);
        
        // If memory is critically low, force congestion mode
        if (free_heap < 20000) { // 20KB is a critical threshold
            s_severe_congestion = true;
            s_last_congestion_time = (uint32_t)(esp_timer_get_time() / 1000);
            ESP_LOGW(TAG, "Memory critically low (%u bytes). Enforcing congestion control.", 
                   free_heap);
        }
        
        vTaskDelay(pdMS_TO_TICKS(MEMORY_CHECK_INTERVAL_MS));
    }
}

static discovered_device_t discovered_devices[MAX_DEVICES];

void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    SAFE_ESP_LOGD(TAG, "GAP event handler called with event: %d", event);
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            // Log raw device address and property count
            SAFE_ESP_LOGD(TAG, "DISC_RES: num_prop=%d", param->disc_res.num_prop);
            ESP_LOG_BUFFER_HEX(TAG, param->disc_res.bda, ESP_BD_ADDR_LEN);
            char bda_str[18];
            uint8_t eir_length = 0;
            uint8_t *eir_name = NULL;

            for (int i = 0; i < param->disc_res.num_prop; i++) {
                esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
                if (p->type == ESP_BT_GAP_DEV_PROP_EIR) {
                    eir_name = esp_bt_gap_resolve_eir_data((uint8_t *)p->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &eir_length);
                    break;
                }
            }

            if (eir_name) {
                strncpy(bda_str, (char *)eir_name, eir_length);
                bda_str[eir_length] = '\0';
            } else {
                strcpy(bda_str, "Unknown");
            }

            bool device_found = false;
            for (int i = 0; i < num_discovered_devices; i++) {
                if (memcmp(discovered_devices[i].bda, param->disc_res.bda, ESP_BD_ADDR_LEN) == 0) {
                    device_found = true;
                    break;
                }
            }

            if (!device_found && num_discovered_devices < MAX_DEVICES) {
                memcpy(discovered_devices[num_discovered_devices].bda, param->disc_res.bda, ESP_BD_ADDR_LEN);
                strncpy(discovered_devices[num_discovered_devices].name, bda_str, ESP_BT_GAP_MAX_BDNAME_LEN);
                discovered_devices[num_discovered_devices].name[ESP_BT_GAP_MAX_BDNAME_LEN] = '\0';
                num_discovered_devices++;
            }

            SAFE_ESP_LOGI(TAG, "Device found: %02x:%02x:%02x:%02x:%02x:%02x, Name: %s",
                     param->disc_res.bda[0], param->disc_res.bda[1], param->disc_res.bda[2],
                     param->disc_res.bda[3], param->disc_res.bda[4], param->disc_res.bda[5], bda_str);
            break;
        }
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            SAFE_ESP_LOGD(TAG, "DISC_STATE_CHANGED: state=%d", param->disc_st_chg.state);
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                SAFE_ESP_LOGI(TAG, "Discovery stopped.");
                SAFE_ESP_LOGI(TAG, "Discovered devices:");
                for (int i = 0; i < num_discovered_devices; i++) {
                    SAFE_ESP_LOGI(TAG, "MAC: %02x:%02x:%02x:%02x:%02x:%02x, Name: %s",
                             discovered_devices[i].bda[0], discovered_devices[i].bda[1], discovered_devices[i].bda[2],
                             discovered_devices[i].bda[3], discovered_devices[i].bda[4], discovered_devices[i].bda[5],
                             discovered_devices[i].name);
                }
                num_discovered_devices = 0;
                memset(discovered_devices, 0, sizeof(discovered_devices));
            }
            break;

        case ESP_BT_GAP_RMT_SRVCS_EVT:
            SAFE_ESP_LOGI(TAG, "Remote device services discovered");
            break;

        case ESP_BT_GAP_RMT_SRVC_REC_EVT:
            SAFE_ESP_LOGI(TAG, "Remote device service record");
            break;

        case ESP_BT_GAP_AUTH_CMPL_EVT:
            SAFE_ESP_LOGD(TAG, "AUTH_CMPL: status=%d", param->auth_cmpl.stat);
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                SAFE_ESP_LOGI(TAG, "Authentication success: %s", param->auth_cmpl.device_name);
                // ...existing success handling...
            } else {
                SAFE_ESP_LOGE(TAG, "Authentication failed, status: %d", param->auth_cmpl.stat);
            }
            break;

        case ESP_BT_GAP_PIN_REQ_EVT:
            SAFE_ESP_LOGI(TAG, "PIN request event received");
            if (!pin_required) {
                // No PIN required - use empty PIN for Just Works pairing
                SAFE_ESP_LOGI(TAG, "No PIN required for this device");
                esp_bt_pin_code_t pin_code = {0};
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 0, pin_code);
            } else {
                // PIN required - use default PIN
                SAFE_ESP_LOGI(TAG, "Using default PIN '0000' for pairing");
                esp_bt_pin_code_t pin_code = {'0', '0', '0', '0'};
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
            }
            break;

        case ESP_BT_GAP_CFM_REQ_EVT:
            SAFE_ESP_LOGI(TAG, "SSP confirmation request: auto-confirming.");
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;

        case ESP_BT_GAP_KEY_NOTIF_EVT:
            // ...existing key notification code...
            break;

        case ESP_BT_GAP_KEY_REQ_EVT:
            // ...existing key request code...
            break;

        case ESP_BT_GAP_READ_RSSI_DELTA_EVT:
        case ESP_BT_GAP_CONFIG_EIR_DATA_EVT:
        case ESP_BT_GAP_SET_AFH_CHANNELS_EVT:
        case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
        case ESP_BT_GAP_MODE_CHG_EVT:
        case ESP_BT_GAP_REMOVE_BOND_DEV_COMPLETE_EVT:
        case ESP_BT_GAP_QOS_CMPL_EVT:
            SAFE_ESP_LOGI(TAG, "ACL event received, ignoring pairing fallback.");
            break;
        case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
            SAFE_ESP_LOGI(TAG, "ACL event received, ignoring pairing fallback.");
            break;
        case ESP_BT_GAP_SET_PAGE_TO_EVT:
        case ESP_BT_GAP_GET_PAGE_TO_EVT:
        case ESP_BT_GAP_ACL_PKT_TYPE_CHANGED_EVT:
        case ESP_BT_GAP_ENC_CHG_EVT:
        case ESP_BT_GAP_SET_MIN_ENC_KEY_SIZE_EVT:
        case ESP_BT_GAP_GET_DEV_NAME_CMPL_EVT:
            SAFE_ESP_LOGI(TAG, "Unhandled GAP event: %d", event);
            break;

        default:
            SAFE_ESP_LOGD(TAG, "Unhandled GAP event %d, raw data:", event);
            ESP_LOG_BUFFER_HEX(TAG, (uint8_t *)param, sizeof(*param));
            break;
    }
}

esp_err_t bluetooth_start_discovery(void) {
    SAFE_ESP_LOGI(TAG, "###Starting Bluetooth device discovery");
    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
        esp_err_t ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 30, 0);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to start discovery: %s", esp_err_to_name(ret));
        }
        xSemaphoreGive(s_bt_resource_mutex);
    }
    SAFE_ESP_LOGI(TAG, "###Leaving bluetooth_start_discovery");
    return ESP_OK;
}

bool is_valid_mac(const char *mac_str) {
    esp_bd_addr_t bd_addr;

    if (strlen(mac_str) != 17) {
        SAFE_ESP_LOGI(TAG, "Invalid MAC address length");
        return false;
    }   

    int cnt = sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
        &bd_addr[0], &bd_addr[1], &bd_addr[2], 
        &bd_addr[3], &bd_addr[4], &bd_addr[5]);

    if (cnt == 1) {
        SAFE_ESP_LOGI(TAG, "bd_addr[0]: %02x", bd_addr[0]);
    }

    return (cnt == 6);
}

// Update bluetooth_pair_device for Echo Buds compatibility
esp_err_t bluetooth_pair_device(const char *mac_str, bool require_pin) {
    // Reset attempt counter if this is a different device
    if (is_valid_mac(mac_str)) {    
        SAFE_ESP_LOGE(TAG, "Invalid MAC address format");
        return ESP_ERR_INVALID_ARG;
    }
    
    /*
    // Check if this is a retry for the same device
    if (memcmp(s_last_pairing_attempt, bd_addr, ESP_BD_ADDR_LEN) != 0) {
        s_pairing_attempt = 0;
        memcpy(s_last_pairing_attempt, bd_addr, ESP_BD_ADDR_LEN);
    }
    
    // Check if we've exceeded retry attempts
    if (s_pairing_attempt >= MAX_PAIRING_ATTEMPTS) {
        SAFE_ESP_LOGE(TAG, "Maximum pairing attempts reached for this device");
        s_pairing_attempt = 0;  // Reset for next time
        return ESP_ERR_TIMEOUT;
    }
    
    s_pairing_attempt++;
    s_pairing_in_progress = true;
    
    // Standard pairing for all devices
    memcpy(pending_pair_addr, bd_addr, ESP_BD_ADDR_LEN);
    pin_required = require_pin;
    
    SAFE_ESP_LOGI(TAG, "Pairing attempt %d/%d with device (MAC=%02x:%02x:%02x:%02x:%02x:%02x), using PIN mode: %s",
             s_pairing_attempt, MAX_PAIRING_ATTEMPTS,
             bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5],
             pin_required ? "yes" : "no");
    */

    // Cancel any ongoing discovery
    esp_bt_gap_cancel_discovery();
    
    // Define IO capability based on PIN requirement
    esp_bt_io_cap_t io_cap = pin_required ? ESP_BT_IO_CAP_OUT : ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io_cap, sizeof(uint8_t));
    
    // Rest of code remains the same...
    return ESP_OK;
}

// Function to disconnect from a paired device
esp_err_t bluetooth_disconnect_device(void) {
    if (s_a2d_state != APP_AV_STATE_CONNECTED && s_a2d_state != APP_AV_STATE_CONNECTING) {
        SAFE_ESP_LOGW(TAG, "No device is currently connected (state=%d)", s_a2d_state);
        return ESP_ERR_INVALID_STATE;
    }

    // Take and hold the mutex for the ENTIRE operation
    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    
    SAFE_ESP_LOGI(TAG, "Attempting to disconnect from device...");
    
    // First stop any streaming
    if (s_media_state == APP_AV_MEDIA_STATE_STARTED) {
        SAFE_ESP_LOGI(TAG, "Stopping media before disconnecting");
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP);
        vTaskDelay(pdMS_TO_TICKS(100)); // Wait for media stop
    }
    
    // Store address locally - CRITICAL: Add defensive check
    esp_bd_addr_t device_addr; // Local variable allocated on the stack
    if (memcmp(pending_pair_addr, (uint8_t[6]){0}, ESP_BD_ADDR_LEN) == 0) {
        // No valid device address
        SAFE_ESP_LOGW(TAG, "No valid device address to disconnect from");
        xSemaphoreGive(s_bt_resource_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(device_addr, pending_pair_addr, ESP_BD_ADDR_LEN); // Memory copied to local variable
    
    // Update state and prepare for waiting
    s_a2d_state = APP_AV_STATE_DISCONNECTING;
    s_operation_complete = false;
    s_waiting_task = xTaskGetCurrentTaskHandle();
    
    // Check memory usage before L2CAP call
    size_t free_heap_before = esp_get_free_heap_size();
    SAFE_ESP_LOGI(TAG, "Free heap before disconnect: %u bytes", free_heap_before);
    
    // Check heap integrity before L2CAP call
    if (!heap_caps_check_integrity_all(true)) {
        SAFE_ESP_LOGE(TAG, "Heap integrity check failed before disconnect");
    }
    
    // Initiate disconnect - we'll keep holding the mutex
    esp_err_t ret = esp_a2d_source_disconnect(device_addr); // L2CAP call
    
    // Check memory usage after L2CAP call
    size_t free_heap_after = esp_get_free_heap_size();
    SAFE_ESP_LOGI(TAG, "Free heap after disconnect: %u bytes", free_heap_after);
    
    // Check heap integrity after L2CAP call
    if (!heap_caps_check_integrity_all(true)) {
        SAFE_ESP_LOGE(TAG, "Heap integrity check failed after disconnect");
    }
    
    if (ret != ESP_OK) {
        // Handle error
        s_a2d_state = APP_AV_STATE_CONNECTED;
        s_waiting_task = NULL;
        xSemaphoreGive(s_bt_resource_mutex);
        return ret;
    }
    
    // Wait for completion while still holding the mutex
    SAFE_ESP_LOGI(TAG, "Disconnect request sent, waiting for completion...");
    
    // Wait with timeout - IMPROVED: Use notification instead of direct flag
    int timeout_counter = 0;
    while (!s_operation_complete && timeout_counter < 10) { // Reduced timeout
        xSemaphoreGive(s_bt_resource_mutex); // Release during wait
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) != pdTRUE) {
            s_waiting_task = NULL; // Clean up task reference
            return ESP_FAIL;
        }
        timeout_counter++;
    }
    
    // Clean up
    s_waiting_task = NULL;
    bool completed = s_operation_complete;
    s_operation_complete = false;
    
    // Only now release the mutex
    xSemaphoreGive(s_bt_resource_mutex);
    
    // Safety: Allow a moment for any pending callbacks to complete
    vTaskDelay(pdMS_TO_TICKS(100));
    
    if (!completed) {
        SAFE_ESP_LOGW(TAG, "Disconnect timed out after 1 second");
        return ESP_ERR_TIMEOUT;
    }
    
    SAFE_ESP_LOGI(TAG, "Disconnect completed successfully");
    return ESP_OK;
}

// Function to unpair a device
esp_err_t bluetooth_unpair_device(void) {
    if (s_a2d_state == APP_AV_STATE_CONNECTED || s_a2d_state == APP_AV_STATE_CONNECTING) {
        if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
            esp_err_t ret = esp_bt_gap_remove_bond_device(pending_pair_addr);
            if (ret != ESP_OK) {
                SAFE_ESP_LOGE(TAG, "Failed to unpair: %s", esp_err_to_name(ret));
                xSemaphoreGive(s_bt_resource_mutex);
                return ret;
            }
            SAFE_ESP_LOGI(TAG, "Unpaired device");
            xSemaphoreGive(s_bt_resource_mutex);
        }
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "No device is currently paired");
        return ESP_ERR_INVALID_STATE;
    }
}

// Function to connect to a paired device
esp_err_t bluetooth_connect_device(const char *mac_str) {
    esp_bd_addr_t bd_addr;
    if (sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
               &bd_addr[0], &bd_addr[1], &bd_addr[2], 
               &bd_addr[3], &bd_addr[4], &bd_addr[5]) != 6) {
        SAFE_ESP_LOGE(TAG, "Invalid MAC address format");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(pending_pair_addr, bd_addr, ESP_BD_ADDR_LEN);

    SAFE_ESP_LOGD(TAG, "Connecting to MAC=%02x:%02x:%02x:%02x:%02x:%02x",
             bd_addr[0], bd_addr[1], bd_addr[2],
             bd_addr[3], bd_addr[4], bd_addr[5]);

    s_a2d_state = APP_AV_STATE_CONNECTING;
    
    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
        // Check memory usage before L2CAP call
        size_t free_heap_before = esp_get_free_heap_size();
        SAFE_ESP_LOGI(TAG, "Free heap before connect: %u bytes", free_heap_before);
        
        // Establish connection
        esp_err_t ret = esp_a2d_source_connect(bd_addr); // L2CAP call
        
        // Check memory usage after L2CAP call
        size_t free_heap_after = esp_get_free_heap_size();
        SAFE_ESP_LOGI(TAG, "Free heap after connect: %u bytes", free_heap_after);
        
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to initiate connection: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }
        xSemaphoreGive(s_bt_resource_mutex);
    }

    // Set default volume after connecting
    esp_err_t ret = bluetooth_set_volume(DEFAULT_VOLUME);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to set default volume after connecting: %s", esp_err_to_name(ret));
    } else {
        SAFE_ESP_LOGI(TAG, "Default volume set after connecting to: %d", DEFAULT_VOLUME);
    }

    return ESP_OK;
}

// Function to restart the Bluetooth stack
esp_err_t restart_bluetooth_stack(void) {
    esp_err_t ret;

    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
        // Disable Bluedroid
        ret = esp_bluedroid_disable();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to disable Bluedroid: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Deinitialize Bluedroid
        ret = esp_bluedroid_deinit();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to deinitialize Bluedroid: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Disable Bluetooth controller
        ret = esp_bt_controller_disable();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to disable Bluetooth controller: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Deinitialize Bluetooth controller
        ret = esp_bt_controller_deinit();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to deinitialize Bluetooth controller: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Reinitialize and enable Bluetooth stack
        ret = bluetooth_init();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to reinitialize Bluetooth stack: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        xSemaphoreGive(s_bt_resource_mutex);
    }

    SAFE_ESP_LOGI(TAG, "Bluetooth stack restarted successfully");
    return ESP_OK;
}

// Function to set the Bluetooth device name
esp_err_t bluetooth_set_device_name(const char *name) {
    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
        esp_err_t ret = esp_bt_gap_set_device_name(name);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to set device name: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Save the device name to NVS
        nvs_handle_t nvs_handle;
        ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        ret = nvs_set_str(nvs_handle, BT_DEVICE_NAME_KEY, name);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to save device name to NVS: %s", esp_err_to_name(ret));
            nvs_close(nvs_handle);
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

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

    // Restart Bluetooth stack to apply the new name
    return restart_bluetooth_stack();
}

// Function to get the Bluetooth device name
esp_err_t bluetooth_get_device_name(char *name, size_t max_len) {
    // Read the device name from NVS
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t name_len = max_len;
    ret = nvs_get_str(nvs_handle, BT_DEVICE_NAME_KEY, name, &name_len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // If not found, use the default name and save it to NVS
        strncpy(name, DEFAULT_BT_DEVICE_NAME, max_len);
        name[max_len - 1] = '\0';  // Ensure null-termination
        ret = nvs_set_str(nvs_handle, BT_DEVICE_NAME_KEY, name);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to save default device name to NVS: %s", esp_err_to_name(ret));
        } else {
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

void bt_app_conn_start_memory_monitor(void)
{
    xTaskCreate(memory_monitor_task, "mem_mon", 2048, NULL, 5, NULL);
}

void bt_app_conn_callback(uint16_t event, void *param) {
    // ...existing code...
    SAFE_ESP_LOGI(TAG, "bt_app_conn_callback: event=%d", event);
    // ...existing code...
}

void bt_app_conn_init(void) {
    // ...existing code...
    SAFE_ESP_LOGI(TAG, "bt_app_conn_init: Initializing connection");
    // ...existing code...
}

void bt_app_conn_deinit(void) {
    // ...existing code...
    SAFE_ESP_LOGI(TAG, "bt_app_conn_deinit: Deinitializing connection");
    // ...existing code...
}