#include "bluetooth/bt_app_conn.h"
#include "bluetooth/bt_app_global.h"
#include "bluetooth/bt_app_init.h"
#include "bluetooth/bt_app_a2dp.h"
#include "bluetooth/bt_app_av.h"
#include "custom_log.h"
#include "esp_bt_defs.h" // Add this line
#include "esp_bt_main.h" // Add this line
#include "esp_gap_bt_api.h" // Add this line
#include "esp_bt_device.h" // Add this line

// Define missing constants if not defined
#ifndef ESP_BT_AUTH_REQ_BONDING
#define ESP_BT_AUTH_REQ_BONDING 0x01
#endif

#ifndef ESP_BT_AUTH_REQ_MITM
#define ESP_BT_AUTH_REQ_MITM 0x04
#endif

#ifndef ESP_BT_SP_AUTHENTICATION_REQUIREMENTS
#define ESP_BT_SP_AUTHENTICATION_REQUIREMENTS 0x0D
#endif

#define TAG "BT_APP_CONN"

// Static variables
static discovered_device_t discovered_devices[MAX_DEVICES];
static TimerHandle_t s_pairing_retry_timer = NULL;
#define PAIRING_RETRY_INTERVAL_MS 3000

// Add this global timer variable so we can refer to it safely
static TimerHandle_t s_connection_timer = NULL;

// Improve the timer handler to be more defensive
static void connection_timeout_handler(TimerHandle_t xTimer) {
    SAFE_ESP_LOGW(TAG, "Bluetooth connection attempt timed out");
    
    // Take the mutex to ensure we don't interfere with ongoing operations
    if (xSemaphoreTake(s_bt_resource_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Only abort if still trying to connect - don't interfere with established connections
        if (s_a2d_state == APP_AV_STATE_CONNECTING) {
            SAFE_ESP_LOGI(TAG, "Connection still in progress - aborting");
            
            // Reset state
            s_a2d_state = APP_AV_STATE_UNCONNECTED;
            s_pairing_in_progress = false;
            
            // Cancel any pending operation
            if (s_waiting_task != NULL) {
                TaskHandle_t waiting_task = s_waiting_task;
                s_operation_complete = true;
                s_waiting_task = NULL;
                xTaskNotifyGive(waiting_task);
            }
        }
        
        xSemaphoreGive(s_bt_resource_mutex);
    }
    
    // Free the timer safely
    s_connection_timer = NULL;
}

// Create a new timer task to periodically check for memory issues
static void memory_monitor_task(void *arg) {
    while(1) {
        size_t free_heap = esp_get_free_heap_size();
        size_t min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
        SAFE_ESP_LOGI(TAG, "Free heap: %u bytes, Minimum free heap: %u bytes", 
                        free_heap, min_free_heap);
        if (free_heap < 20000) { // 20KB is a critical threshold
            s_severe_congestion = true;
            s_last_congestion_time = (uint32_t)(esp_timer_get_time() / 1000);
            ESP_LOGW(TAG, "Memory critically low (%u bytes). Enforcing congestion control.", 
                     free_heap);
        }
        vTaskDelay(pdMS_TO_TICKS(MEMORY_CHECK_INTERVAL_MS));
    }
}

// The same for the pairing retry timer callback
static void pairing_retry_timer_callback(TimerHandle_t xTimer) {
    if (s_pairing_in_progress && s_pairing_attempt < MAX_PAIRING_ATTEMPTS) {
        SAFE_ESP_LOGI(TAG, "Auto-retrying pairing (attempt %d/%d)",
                s_pairing_attempt + 1, MAX_PAIRING_ATTEMPTS);
                
        // Use the stored MAC address and PIN setting from previous attempt
        esp_bd_addr_t retry_addr;
        memcpy(retry_addr, s_last_pairing_attempt, ESP_BD_ADDR_LEN);
        
        // Increment attempt counter
        s_pairing_attempt++;
        
        // Reconnect with the same parameters as before
        
        // Add a small delay before retrying
        vTaskDelay(pdMS_TO_TICKS(500));
        
        esp_err_t ret = esp_a2d_source_connect(retry_addr);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Retry failed: %s", esp_err_to_name(ret));
            
            if (s_pairing_attempt < MAX_PAIRING_ATTEMPTS) {
                // Try again after delay
                xTimerStart(s_pairing_retry_timer, 0);
            } else {
                SAFE_ESP_LOGE(TAG, "Maximum retry attempts reached.");
                s_pairing_in_progress = false;
            }
        }
    } else {
        // Either pairing is no longer in progress or max attempts reached
        s_pairing_in_progress = false;
    }
}

void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    SAFE_ESP_LOGD(TAG, "GAP event handler called with event: %d", event);
    char addr_str[18]; // declare once for this function
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            // Log raw device address and property count
            SAFE_ESP_LOGD(TAG, "DISC_RES: num_prop=%d", param->disc_res.num_prop);
            ESP_LOG_BUFFER_HEX(TAG, param->disc_res.bda, ESP_BD_ADDR_LEN);
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
                strncpy(addr_str, (char *)eir_name, eir_length);
                addr_str[eir_length] = '\0';
            } else {
                strcpy(addr_str, "Unknown");
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
                strncpy(discovered_devices[num_discovered_devices].name, addr_str, ESP_BT_GAP_MAX_BDNAME_LEN);
                discovered_devices[num_discovered_devices].name[ESP_BT_GAP_MAX_BDNAME_LEN] = '\0';
                num_discovered_devices++;
            }

            SAFE_ESP_LOGI(TAG, "Device found: %02x:%02x:%02x:%02x:%02x:%02x, Name: %s",
                     param->disc_res.bda[0], param->disc_res.bda[1], param->disc_res.bda[2],
                     param->disc_res.bda[3], param->disc_res.bda[4], param->disc_res.bda[5], addr_str);
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

        case ESP_BT_GAP_AUTH_CMPL_EVT:
            SAFE_ESP_LOGD(TAG, "AUTH_CMPL: status=%d", param->auth_cmpl.stat);
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                SAFE_ESP_LOGI(TAG, "Authentication success: %s", param->auth_cmpl.device_name);
                s_pairing_in_progress = false; // stop retry process
                if (s_pairing_retry_timer != NULL && xTimerIsTimerActive(s_pairing_retry_timer)) {
                    xTimerStop(s_pairing_retry_timer, 0);
                }
            } else {
                SAFE_ESP_LOGE(TAG, "Authentication failed, status: %d", param->auth_cmpl.stat);
                s_pairing_in_progress = false;
                if (s_pairing_retry_timer != NULL && xTimerIsTimerActive(s_pairing_retry_timer)) {
                    xTimerStop(s_pairing_retry_timer, 0);
                }
                if (s_a2d_state == APP_AV_STATE_CONNECTING) {
                    s_a2d_state = APP_AV_STATE_UNCONNECTED;
                }
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

        // For ACL events, simply log without reiterating a second local addr_str.
        case ESP_BT_GAP_KEY_NOTIF_EVT:
        case ESP_BT_GAP_KEY_REQ_EVT:
        case ESP_BT_GAP_READ_RSSI_DELTA_EVT:
        case ESP_BT_GAP_CONFIG_EIR_DATA_EVT:
        case ESP_BT_GAP_SET_AFH_CHANNELS_EVT:
        case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
        case ESP_BT_GAP_MODE_CHG_EVT:
        case ESP_BT_GAP_REMOVE_BOND_DEV_COMPLETE_EVT:
        case ESP_BT_GAP_QOS_CMPL_EVT:
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

bool is_valid_mac(const char *mac_str) {
    esp_bd_addr_t bd_addr;
    
    // Log the MAC string for debugging
    SAFE_ESP_LOGI(TAG, "Validating MAC address: %s", mac_str);
    
    // Verify length
    if (strlen(mac_str) != 17) {
        SAFE_ESP_LOGW(TAG, "Invalid MAC address length: %d (expected 17)", strlen(mac_str));
        return false;
    }
    
    // Try manual parsing as an alternative to sscanf
    // Format should be xx:xx:xx:xx:xx:xx where xx is a hexadecimal number
    char hex_str[3] = {0}; // Space for 2 hex chars plus null terminator
    
    for (int i = 0; i < 6; i++) {
        // Extract 2 hex chars
        hex_str[0] = mac_str[i*3];
        hex_str[1] = mac_str[i*3+1];
        hex_str[2] = '\0';
        
        // Verify they're valid hex
        for (int j = 0; j < 2; j++) {
            if (!((hex_str[j] >= '0' && hex_str[j] <= '9') || 
                  (hex_str[j] >= 'a' && hex_str[j] <= 'f') || 
                  (hex_str[j] >= 'A' && hex_str[j] <= 'F'))) {
                SAFE_ESP_LOGW(TAG, "Invalid hex character in MAC address: %c", hex_str[j]);
                return false;
            }
        }
        
        // Convert to byte
        char *endptr = NULL;
        bd_addr[i] = (uint8_t)strtol(hex_str, &endptr, 16);
        
        // Check correct conversion
        if (endptr != hex_str + 2) {
            SAFE_ESP_LOGW(TAG, "Failed to parse hex value: %s", hex_str);
            return false;
        }
        
        // Check for colon separator (except for the last byte)
        if (i < 5 && mac_str[i*3+2] != ':') {
            SAFE_ESP_LOGW(TAG, "Missing colon separator in MAC address");
            return false;
        }
    }
    
    // If we get here, the MAC address is valid
    SAFE_ESP_LOGI(TAG, "MAC validated: %02x:%02x:%02x:%02x:%02x:%02x", 
                 bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5]);
    
    // Store it for use in bluetooth_pair_device
    memcpy(pending_pair_addr, bd_addr, ESP_BD_ADDR_LEN);
    
    return true;
}

esp_err_t bluetooth_pair_device(const char *mac_str, bool require_pin) {
    // Validate and parse MAC address
    if (!is_valid_mac(mac_str)) {
        SAFE_ESP_LOGE(TAG, "Invalid MAC address format: %s", mac_str);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Delete any existing timer first
    if (s_connection_timer != NULL) {
        xTimerStop(s_connection_timer, 0);
        xTimerDelete(s_connection_timer, 0);
        s_connection_timer = NULL;
    }
    
    // Create a connection timeout timer
    s_connection_timer = xTimerCreate(
        "conn_timeout",
        pdMS_TO_TICKS(30000), // 30 seconds timeout
        pdFALSE,  // One-shot timer
        NULL,
        connection_timeout_handler
    );
    
    if (s_connection_timer == NULL) {
        SAFE_ESP_LOGE(TAG, "Failed to create connection timeout timer");
        return ESP_ERR_NO_MEM;
    }
    
    // No need to parse the MAC again, is_valid_mac already stored it in pending_pair_addr
    SAFE_ESP_LOGI(TAG, "Starting pairing with device: %02x:%02x:%02x:%02x:%02x:%02x", 
             pending_pair_addr[0], pending_pair_addr[1], pending_pair_addr[2], 
             pending_pair_addr[3], pending_pair_addr[4], pending_pair_addr[5]);
    
    // Store device info for later use
    pin_required = require_pin;
    
    // Track this device for auto-retry implementation
    if (memcmp(s_last_pairing_attempt, pending_pair_addr, ESP_BD_ADDR_LEN) != 0) {
        // Reset counter for new device
        s_pairing_attempt = 0;
        memcpy(s_last_pairing_attempt, pending_pair_addr, ESP_BD_ADDR_LEN);
    }
    
    // Log attempt number but don't prevent manual retries
    s_pairing_attempt++;
    SAFE_ESP_LOGI(TAG, "Manual pairing attempt %d for this device", s_pairing_attempt);
    
    // Always allow manual pairing attempts from user, regardless of counter
    s_pairing_in_progress = true;
    
    // Cancel any ongoing discovery
    esp_bt_gap_cancel_discovery();
    SAFE_ESP_LOGI(TAG, "Discovery canceled to start pairing");
    
    // First, set pin type and code
    if (require_pin) {
        // Fixed PIN for devices that need a PIN
        SAFE_ESP_LOGI(TAG, "Setting fixed PIN '0000'");
        esp_bt_pin_code_t pin_code = {'0', '0', '0', '0'};
        esp_err_t ret = esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, 4, pin_code);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to set PIN: %s", esp_err_to_name(ret));
            xTimerDelete(s_connection_timer, 0);
            return ret;
        }
    } else {
        // For Just Works pairing, we set a variable PIN that won't actually be used
        SAFE_ESP_LOGI(TAG, "Setting up for 'Just Works' pairing");
        esp_bt_pin_code_t pin_code = {0};
        esp_err_t ret = esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 0, pin_code);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to set PIN: %s", esp_err_to_name(ret));
            xTimerDelete(s_connection_timer, 0);
            return ret;
        }
    }
    
    // Second, set IO capability
    esp_bt_io_cap_t io_cap = require_pin ? ESP_BT_IO_CAP_OUT : ESP_BT_IO_CAP_NONE;
    SAFE_ESP_LOGI(TAG, "Setting IO capability: %d", io_cap);
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io_cap, sizeof(uint8_t));
    
    // Add logging here
    uint8_t auth_req = ESP_BT_AUTH_REQ_BONDING | ESP_BT_AUTH_REQ_MITM; // Bonding and MITM protection
    
    // Remove MITM protection if not the first attempt
    if (s_pairing_attempt > 1) {
        auth_req &= ~ESP_BT_AUTH_REQ_MITM;
        SAFE_ESP_LOGI(TAG, "Pairing attempt %d: Removing MITM protection", s_pairing_attempt);
    }
    
    SAFE_ESP_LOGI(TAG, "Setting authentication requirements: 0x%x", auth_req);
    esp_bt_gap_set_security_param(ESP_BT_SP_AUTHENTICATION_REQUIREMENTS, &auth_req, sizeof(uint8_t));
    
    // Make the device discoverable
    SAFE_ESP_LOGI(TAG, "Setting device as discoverable");
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    
    // IMPORTANT: Wait a moment for security settings to take effect
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Now initiate the connection
    SAFE_ESP_LOGI(TAG, "Initiating A2DP source connection...");
    s_a2d_state = APP_AV_STATE_CONNECTING;
    
    // Check heap integrity before L2CAP call
    if (!heap_caps_check_integrity_all(true)) {
        SAFE_ESP_LOGE(TAG, "Heap integrity check failed before connect (L2CAP)");
    }
    
    // Start the connection timeout timer
    if (xTimerStart(s_connection_timer, 0) != pdPASS) {
        SAFE_ESP_LOGE(TAG, "Failed to start connection timeout timer");
        xTimerDelete(s_connection_timer, 0);
        s_connection_timer = NULL;
        // Continue anyway
    }
    
    esp_err_t ret = esp_a2d_source_connect(pending_pair_addr);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to initiate connection: %s", esp_err_to_name(ret));
        xTimerStop(s_connection_timer, 0);
        xTimerDelete(s_connection_timer, 0);
        s_connection_timer = NULL;
        return ret;
    }
    
    // Add logging here
    SAFE_ESP_LOGI(TAG, "A2DP connection initiated successfully");
    
    // Check heap integrity after L2CAP call
    if (!heap_caps_check_integrity_all(true)) {
        SAFE_ESP_LOGE(TAG, "Heap integrity check failed after connect (L2CAP)");
    }
    
    SAFE_ESP_LOGI(TAG, "Connection request sent, pairing should follow");
    return ESP_OK;
}

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
    
    // Wait with timeout
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

esp_err_t bluetooth_connect_device(const char *mac_str) {
    // Use the same validation function for consistency
    if (!is_valid_mac(mac_str)) {
        SAFE_ESP_LOGE(TAG, "Invalid MAC address format");
        return ESP_ERR_INVALID_ARG;
    }

    // No need to parse again, is_valid_mac already stored the result in pending_pair_addr
    SAFE_ESP_LOGD(TAG, "Connecting to MAC=%02x:%02x:%02x:%02x:%02x:%02x",
             pending_pair_addr[0], pending_pair_addr[1], pending_pair_addr[2],
             pending_pair_addr[3], pending_pair_addr[4], pending_pair_addr[5]);

    s_a2d_state = APP_AV_STATE_CONNECTING;
    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
        // Check memory usage before L2CAP call
        size_t free_heap_before = esp_get_free_heap_size();
        SAFE_ESP_LOGI(TAG, "Free heap before connect: %u bytes", free_heap_before);
        
        // Use the stored address from is_valid_mac
        esp_err_t ret = esp_a2d_source_connect(pending_pair_addr); // L2CAP call
        
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

void bt_app_conn_start_memory_monitor(void) {
    xTaskCreate(memory_monitor_task, "mem_mon", 2048, NULL, 5, NULL);
}

void bt_app_conn_callback(uint16_t event, void *param) {
    // This function is called from the Bluetooth stack with events
    esp_a2d_cb_param_t *a2d = (esp_a2d_cb_param_t *)param;

    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT: {
            uint8_t *bda = a2d->conn_stat.remote_bda;
            SAFE_ESP_LOGI(TAG, "A2DP connection state: %s, [%02x:%02x:%02x:%02x:%02x:%02x]",
                     s_a2d_conn_state_str[a2d->conn_stat.state],
                     bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
            if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
                if (s_waiting_task != NULL) {
                    s_operation_complete = true;
                    xTaskNotifyGive(s_waiting_task);
                }
                s_a2d_state = APP_AV_STATE_UNCONNECTED;
            } else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                // When connected, update state and stop being discoverable
                esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
                s_a2d_state = APP_AV_STATE_CONNECTED;
            } else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTING) {
                s_a2d_state = APP_AV_STATE_CONNECTING;
            }
            break;
        }
        case ESP_A2D_AUDIO_STATE_EVT:
            SAFE_ESP_LOGI(TAG, "A2DP audio state: %s", s_a2d_audio_state_str[a2d->audio_stat.state]);
            s_audio_state = a2d->audio_stat.state; 
            if (s_audio_state == ESP_A2D_AUDIO_STATE_STARTED) {
                s_media_state = APP_AV_MEDIA_STATE_STARTED;
            } else if (s_audio_state == ESP_A2D_AUDIO_STATE_STOPPED || 
                       s_audio_state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) {
                s_media_state = APP_AV_MEDIA_STATE_STOPPED;
            }
            break;
            
        case ESP_A2D_MEDIA_CTRL_ACK_EVT:
            SAFE_ESP_LOGI(TAG, "A2DP media ctrl ack: %d", a2d->media_ctrl_stat.cmd);
            break;
            
        default:
            SAFE_ESP_LOGW(TAG, "Unhandled A2DP event: %d", event);
            break;
    }
}

void bt_app_conn_init(void) {
    // Initialize connection variables
    s_pairing_in_progress = false;
    s_pairing_attempt = 0;
    s_a2d_state = APP_AV_STATE_IDLE;
    s_operation_complete = false;
    s_waiting_task = NULL;
    s_audio_state = ESP_A2D_AUDIO_STATE_STOPPED;
    s_media_state = APP_AV_MEDIA_STATE_IDLE;

    // Set default device name if needed
    char device_name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
    if (bluetooth_get_device_name(device_name, sizeof(device_name)) == ESP_OK) {
        esp_bt_gap_set_device_name(device_name);
    } else {
        esp_bt_gap_set_device_name(DEFAULT_BT_DEVICE_NAME);
    }

    // Create the pairing retry timer
    s_pairing_retry_timer = xTimerCreate("pairing_retry", 
                                     pdMS_TO_TICKS(PAIRING_RETRY_INTERVAL_MS),
                                     pdFALSE,  // One-shot timer
                                     NULL, 
                                     pairing_retry_timer_callback);

    // Set as connectable and discoverable
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    SAFE_ESP_LOGI(TAG, "Bluetooth connection initialized successfully");
}

void bt_app_conn_deinit(void) {
    // Clean up timers
    if (s_pairing_retry_timer != NULL) {
        if (xTimerIsTimerActive(s_pairing_retry_timer)) {
            xTimerStop(s_pairing_retry_timer, 0);
        }
        xTimerDelete(s_pairing_retry_timer, 0);
        s_pairing_retry_timer = NULL;
    }

    // Reset state variables
    s_pairing_in_progress = false;
    s_pairing_attempt = 0;
    s_a2d_state = APP_AV_STATE_IDLE;
    s_operation_complete = false;
    s_waiting_task = NULL;
    s_audio_state = ESP_A2D_AUDIO_STATE_STOPPED;
    s_media_state = APP_AV_MEDIA_STATE_IDLE;

    SAFE_ESP_LOGI(TAG, "Bluetooth connection deinitialized");
}