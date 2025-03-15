/**
 * @file bt_app_conn.c
 * @brief Bluetooth connection management functionality
 * 
 * This file implements connection management for the Bluetooth A2DP source profile,
 * including device connection, disconnection, pairing and reconnection logic.
 * It handles Bluetooth operation timeouts, pairing retries, and provides an interface
 * for other components to control Bluetooth connections.
 */
#include "bluetooth/bt_app_conn.h"
#include "bluetooth/bt_app_global.h"
#include "bluetooth/bt_app_init.h"
#include "bluetooth/bt_app_a2dp.h"
#include "bluetooth/bt_app_av.h"
#include "bluetooth/bt_app_monitor.h"
#include "bluetooth/bt_app_discovery.h"
#include "bluetooth/bt_app_name.h"
#include "bluetooth/bt_app_utils.h"
#include "custom_log.h"
#include "esp_bt_defs.h"      // Bluetooth definitions
#include "esp_bt_main.h"      // Bluetooth main API
#include "esp_gap_bt_api.h"   // Generic Access Profile API
#include "esp_bt_device.h"    // Bluetooth device functions
#include "radio.h"            // Radio control functions

#define TAG "BT_APP_CONN"

/* Timer handle for managing connection timeouts */
static TimerHandle_t connection_timer = NULL;

/**
 * @brief Handles connection timeout events
 * 
 * This function is called when a Bluetooth connection attempt times out.
 * It safely aborts the connection attempt and resets the connection state.
 * 
 * @param xTimer Timer handle that triggered this callback
 */
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
    connection_timer = NULL;
}

/**
 * @brief Pairing retry timer callback function
 * 
 * Attempts to automatically retry pairing if the previous attempt failed.
 * Tracks attempts and will stop retrying after reaching MAX_PAIRING_ATTEMPTS.
 * 
 * @param xTimer Timer handle that triggered this callback
 */
static void pairing_retry_timer_callback(TimerHandle_t xTimer) {
    if (s_pairing_in_progress && s_pairing_attempt < MAX_PAIRING_ATTEMPTS) {
        SAFE_ESP_LOGI(TAG, "Auto-retrying pairing (attempt %d/%d)",
                s_pairing_attempt + 1, MAX_PAIRING_ATTEMPTS);
                
        // Use the stored MAC address and PIN setting from previous attempt
        esp_bd_addr_t retry_addr;
        memcpy(retry_addr, s_last_pairing_attempt, ESP_BD_ADDR_LEN);
        
        // Increment attempt counter
        s_pairing_attempt++;
        
        // Add a small delay before retrying
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // Initiate a new connection attempt
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

/**
 * @brief Pair with a Bluetooth device 
 * 
 * Establishes a Bluetooth pairing with a device specified by its MAC address.
 * Can use PIN authentication if required, and implements retry mechanisms.
 * 
 * @param mac_str MAC address string in format "XX:XX:XX:XX:XX:XX"
 * @param require_pin Whether PIN code is required for pairing
 * @return ESP_OK if pairing initiated successfully, error code otherwise
 */
esp_err_t bluetooth_pair_device(const char *mac_str, bool require_pin) {
    // Validate and parse MAC address
    if (!is_valid_mac(mac_str)) {
        SAFE_ESP_LOGE(TAG, "Invalid MAC address format: %s", mac_str);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Delete any existing timer first to prevent resource leaks
    if (connection_timer != NULL) {
        xTimerStop(connection_timer, 0);
        xTimerDelete(connection_timer, 0);
        connection_timer = NULL;
    }
    
    // Create a connection timeout timer
    connection_timer = xTimerCreate(
        "conn_timeout",
        pdMS_TO_TICKS(30000), // 30 seconds timeout
        pdFALSE,  // One-shot timer
        NULL,
        connection_timeout_handler
    );
    
    if (connection_timer == NULL) {
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
    
    // Cancel any ongoing discovery as it can interfere with pairing
    esp_bt_gap_cancel_discovery();
    SAFE_ESP_LOGI(TAG, "Discovery canceled to start pairing");
    
    // If already connected or connecting, force disconnect to flush ACL queue
    if (s_a2d_state == APP_AV_STATE_CONNECTED || s_a2d_state == APP_AV_STATE_CONNECTING) {
        SAFE_ESP_LOGI(TAG, "Device already in use; forcing disconnect to clear ACL transmissions");
        esp_err_t dis_ret = bluetooth_disconnect_device();
        if (dis_ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to disconnect existing device: %s", esp_err_to_name(dis_ret));
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for state to settle
    }
    
    // Stop any ongoing radio streaming to prevent resource conflicts
    esp_err_t ret_radio = radio_stop();
    if (ret_radio != ESP_OK) {
        SAFE_ESP_LOGW(TAG, "radio_stop returned %s; proceeding anyway", esp_err_to_name(ret_radio));
    }
    
    // Wait for radio task to finish to ensure clean state
    int wait = 0;
    while (!radio_task_is_finished() && wait < 100) {
        vTaskDelay(pdMS_TO_TICKS(10));
        wait++;
    }
    
    if (!radio_task_is_finished()) {
        SAFE_ESP_LOGW(TAG, "Radio task did not finish after %d ticks", wait);
    } else {
        SAFE_ESP_LOGI(TAG, "Radio task confirmed finished");
    }
    
    // Configure PIN type and code based on device requirements
    if (require_pin) {
        // Fixed PIN for devices that need a PIN
        SAFE_ESP_LOGI(TAG, "Setting fixed PIN '0000'");
        esp_bt_pin_code_t pin_code = {'0', '0', '0', '0'};
        esp_err_t ret = esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, 4, pin_code);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to set PIN: %s", esp_err_to_name(ret));
            xTimerDelete(connection_timer, 0);
            return ret;
        }
    } else {
        // For Just Works pairing, we set a variable PIN that won't actually be used
        SAFE_ESP_LOGI(TAG, "Setting up for 'Just Works' pairing");
        esp_bt_pin_code_t pin_code = {0};
        esp_err_t ret = esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 0, pin_code);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to set PIN: %s", esp_err_to_name(ret));
            xTimerDelete(connection_timer, 0);
            return ret;
        }
    }
    
    // Set IO capability based on whether PIN is required
    esp_bt_io_cap_t io_cap = require_pin ? ESP_BT_IO_CAP_OUT : ESP_BT_IO_CAP_NONE;
    SAFE_ESP_LOGI(TAG, "Setting IO capability: %d", io_cap);
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io_cap, sizeof(uint8_t));
    
    // Configure authentication requirements
    uint8_t auth_req = ESP_BT_AUTH_REQ_BONDING | ESP_BT_AUTH_REQ_MITM; // Bonding and MITM protection
    
    // Remove MITM protection if not the first attempt for improved compatibility
    if (s_pairing_attempt > 1) {
        auth_req &= ~ESP_BT_AUTH_REQ_MITM;
        SAFE_ESP_LOGI(TAG, "Pairing attempt %d: Removing MITM protection", s_pairing_attempt);
    }
    
    SAFE_ESP_LOGI(TAG, "Setting authentication requirements: 0x%x", auth_req);
    esp_bt_gap_set_security_param(ESP_BT_SP_AUTHENTICATION_REQUIREMENTS, &auth_req, sizeof(uint8_t));
    
    // Make the device discoverable to enable pairing
    SAFE_ESP_LOGI(TAG, "Setting device as discoverable");
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    
    // Wait for security settings to take effect
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Initiate the connection
    SAFE_ESP_LOGI(TAG, "Initiating A2DP source connection...");
    s_a2d_state = APP_AV_STATE_CONNECTING;
    
    // Check heap integrity before L2CAP call
    if (!heap_caps_check_integrity_all(true)) {
        SAFE_ESP_LOGE(TAG, "Heap integrity check failed before connect (L2CAP)");
    }
    
    // Start the connection timeout timer
    if (xTimerStart(connection_timer, 0) != pdPASS) {
        SAFE_ESP_LOGE(TAG, "Failed to start connection timeout timer");
        xTimerDelete(connection_timer, 0);
        connection_timer = NULL;
        // Continue anyway
    }
    
    // Initiate the A2DP connection
    esp_err_t ret = esp_a2d_source_connect(pending_pair_addr);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to initiate connection: %s", esp_err_to_name(ret));
        xTimerStop(connection_timer, 0);
        xTimerDelete(connection_timer, 0);
        connection_timer = NULL;
        return ret;
    }
    
    SAFE_ESP_LOGI(TAG, "A2DP connection initiated successfully");
    
    // Check heap integrity after L2CAP call to detect memory issues
    if (!heap_caps_check_integrity_all(true)) {
        SAFE_ESP_LOGE(TAG, "Heap integrity check failed after connect (L2CAP)");
    }
    
    SAFE_ESP_LOGI(TAG, "Connection request sent, pairing should follow");
    return ESP_OK;
}

/**
 * @brief Disconnect from currently connected device
 * 
 * Safely disconnects from the currently connected Bluetooth device.
 * Stops any active media streaming first, then initiates disconnection.
 * 
 * @return ESP_OK if disconnect successful, error code otherwise
 */
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
    
    // First stop any streaming to ensure clean disconnection
    if (s_media_state == APP_AV_MEDIA_STATE_STARTED) {
        SAFE_ESP_LOGI(TAG, "Stopping media before disconnecting");
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP);    
        vTaskDelay(pdMS_TO_TICKS(100)); // Wait for media stop
    }
    
    // Store address locally with defensive check to prevent null address issues
    esp_bd_addr_t device_addr; // Local variable allocated on the stack
    if (memcmp(pending_pair_addr, (uint8_t[6]){0}, ESP_BD_ADDR_LEN) == 0) {
        // No valid device address
        SAFE_ESP_LOGW(TAG, "No valid device address to disconnect from");
        xSemaphoreGive(s_bt_resource_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(device_addr, pending_pair_addr, ESP_BD_ADDR_LEN);
    
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
    esp_err_t ret = esp_a2d_source_disconnect(device_addr);
    
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
    
    // Wait for completion with timeout while still holding the mutex
    SAFE_ESP_LOGI(TAG, "Disconnect request sent, waiting for completion...");
    
    // Wait with timeout (1 second total)
    int timeout_counter = 0;
    while (!s_operation_complete && timeout_counter < 10) { // 10 * 100ms = 1 second
        xSemaphoreGive(s_bt_resource_mutex); // Release during wait
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) != pdTRUE) {
            s_waiting_task = NULL; // Clean up task reference
            return ESP_FAIL;
        }
        timeout_counter++;
    }
    
    // Clean up waiting state
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

/**
 * @brief Remove device from bonded devices list
 * 
 * Unpairs/unbonds from the current or specified Bluetooth device.
 * 
 * @return ESP_OK if unpair successful, error code otherwise
 */
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

/**
 * @brief Connect to a previously paired device
 * 
 * Establishes a connection with a device specified by MAC address.
 * Does not perform pairing, but connects to an already paired device.
 * Also sets default volume after connection.
 * 
 * @param mac_str MAC address string in format "XX:XX:XX:XX:XX:XX"
 * @return ESP_OK if connection initiated successfully, error code otherwise
 */
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
        esp_err_t ret = esp_a2d_source_connect(pending_pair_addr);
        
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

/**
 * @brief A2DP connection callback handler
 * 
 * Processes A2DP events received from the Bluetooth stack.
 * Updates connection state and handles state transitions.
 * 
 * @param event A2DP event type
 * @param param Event parameters
 */
void bt_app_conn_callback(uint16_t event, void *param) {
    // This function is called from the Bluetooth stack with events
    esp_a2d_cb_param_t *a2d = (esp_a2d_cb_param_t *)param;

    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT: {
            // Handle A2DP connection state changes
            uint8_t *bda = a2d->conn_stat.remote_bda;
            SAFE_ESP_LOGI(TAG, "A2DP connection state: %s, [%02x:%02x:%02x:%02x:%02x:%02x]",
                     s_a2d_conn_state_str[a2d->conn_stat.state],
                     bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
            
            if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                // When disconnected, return to discoverable mode
                esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
                
                // Notify any waiting tasks
                if (s_waiting_task != NULL) {
                    s_operation_complete = true;
                    xTaskNotifyGive(s_waiting_task);
                }
                s_a2d_state = APP_AV_STATE_UNCONNECTED;
                
            } else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                // When connected, become non-discoverable to prevent other devices from finding us
                esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
                s_a2d_state = APP_AV_STATE_CONNECTED;
                
            } else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTING) {
                s_a2d_state = APP_AV_STATE_CONNECTING;
            }
            break;
        }
        
        case ESP_A2D_AUDIO_STATE_EVT:
            // Handle A2DP audio streaming state changes
            SAFE_ESP_LOGI(TAG, "A2DP audio state: %s", s_a2d_audio_state_str[a2d->audio_stat.state]);
            s_audio_state = a2d->audio_stat.state; 
            
            // Update media state based on audio state
            if (s_audio_state == ESP_A2D_AUDIO_STATE_STARTED) {
                s_media_state = APP_AV_MEDIA_STATE_STARTED;
            } else if (s_audio_state == ESP_A2D_AUDIO_STATE_STOPPED || 
                       s_audio_state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) {
                s_media_state = APP_AV_MEDIA_STATE_STOPPED;
            }
            break;
            
        case ESP_A2D_MEDIA_CTRL_ACK_EVT:
            // Log media control acknowledgments
            SAFE_ESP_LOGI(TAG, "A2DP media ctrl ack: %d", a2d->media_ctrl_stat.cmd);
            break;
            
        default:
            // Log any unhandled events
            SAFE_ESP_LOGW(TAG, "Unhandled A2DP event: %d", event);
            break;
    }
}

/**
 * @brief Initialize the Bluetooth connection module
 * 
 * Sets up connection and pairing state variables, device name,
 * retry timer, and discovery mode.
 */
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

    // Create the pairing retry timer for automatic retry attempts
    s_pairing_retry_timer = xTimerCreate("pairing_retry", 
                                     pdMS_TO_TICKS(PAIRING_RETRY_INTERVAL_MS),
                                     pdFALSE,  // One-shot timer
                                     NULL, 
                                     pairing_retry_timer_callback);

    // Set as connectable and discoverable to allow pairing
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    SAFE_ESP_LOGI(TAG, "Bluetooth connection initialized successfully");
}

/**
 * @brief Clean up Bluetooth connection resources
 * 
 * Frees timers and resets state variables when the 
 * Bluetooth connection module is no longer needed.
 */
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