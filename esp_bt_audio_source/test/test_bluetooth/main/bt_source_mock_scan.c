/* bt_source_mock_scan.c — inquiry/scan, paired-device registry + reconnect mocks.
 * Split out of bt_source_mock.c; shares bt_source_mock_internal.h. */
#include "bt_source_mock_internal.h"

static const char *TAG = "BT_SOURCE_MOCK";

static void scan_timeout_callback(TimerHandle_t timer);


/**
 * Start Bluetooth device scan without timeout (matches bt_scan_start API)
 */
esp_err_t bt_scan_start(void)
{
    ESP_LOGI(TAG, "Mock: Starting Bluetooth scan");

    if (!s_initialized && mock_control.scan_start_return == ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    if (mock_control.scan_start_return != ESP_OK) {
        s_scan_active = false;
        return mock_control.scan_start_return;
    }

    cancel_scan_timer();

#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    /* Delegate to the component-level mock so authoritative scan state/results
     * stay in sync with bt_mock. */
    bt_mock_start_scan();
    s_scan_active = bt_mock_is_scanning();
    s_discovered_device_count = bt_mock_get_scan_results(s_discovered_devices, MAX_DISCOVERED_DEVICES);
#else
    s_scan_active = true;
#endif

    return ESP_OK;
}

/**
 * Start Bluetooth device scan with timeout
 */
esp_err_t bt_scan(uint32_t timeout_seconds)
{
    ESP_LOGI(TAG, "Mock: Starting Bluetooth scan with timeout %" PRIu32 "s", timeout_seconds);

    if (!s_initialized && mock_control.scan_start_return == ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    if (mock_control.scan_start_return != ESP_OK) {
        s_scan_active = false;
        return mock_control.scan_start_return;
    }

    cancel_scan_timer();

#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    bt_mock_start_scan();
    s_scan_active = bt_mock_is_scanning();
    s_discovered_device_count = bt_mock_get_scan_results(s_discovered_devices, MAX_DISCOVERED_DEVICES);
#else
    s_scan_active = true;
#endif

    if (timeout_seconds > 0U) {
        uint64_t timeout_ms = (uint64_t)timeout_seconds * 1000ULL;
        if (timeout_ms > UINT32_MAX) {
            timeout_ms = UINT32_MAX;
        }
        TickType_t timeout_ticks = pdMS_TO_TICKS((uint32_t)timeout_ms);
        if (timeout_ticks == 0) {
            timeout_ticks = 1;
        }

        if (s_scan_timer == NULL) {
            s_scan_timer = xTimerCreate("scan_timeout",
                                        timeout_ticks,
                                        pdFALSE,
                                        NULL,
                                        scan_timeout_callback);
            if (s_scan_timer == NULL) {
                ESP_LOGE(TAG, "Failed to allocate scan timeout timer");
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
                bt_mock_stop_scan();
                s_scan_active = bt_mock_is_scanning();
#else
                s_scan_active = false;
#endif
                return ESP_ERR_NO_MEM;
            }
        } else {
            if (xTimerIsTimerActive(s_scan_timer) != pdFALSE) {
                (void)xTimerStop(s_scan_timer, 0);
            }
            if (xTimerChangePeriod(s_scan_timer, timeout_ticks, 0) != pdPASS) {
                ESP_LOGE(TAG, "Failed to set scan timeout period");
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
                bt_mock_stop_scan();
                s_scan_active = bt_mock_is_scanning();
#else
                s_scan_active = false;
#endif
                return ESP_FAIL;
            }
        }

        if (xTimerStart(s_scan_timer, 0) != pdPASS) {
            ESP_LOGE(TAG, "Failed to start scan timeout timer");
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
            bt_mock_stop_scan();
            s_scan_active = bt_mock_is_scanning();
#else
            s_scan_active = false;
#endif
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

/**
 * Start Bluetooth device scan with filtering
 *
 * Note: Implementation matches the header - only device_type parameter
 */
esp_err_t bt_scan_start_filtered(bt_device_type_t device_type)
{
    ESP_LOGI(TAG, "Mock: Starting filtered Bluetooth scan");
    s_current_filter = device_type;
    return bt_scan_start();
}

/**
 * Stop Bluetooth device scan
 */
esp_err_t bt_scan_stop(void)
{
    ESP_LOGI(TAG, "Mock: Stopping Bluetooth scan");

    if (!s_scan_active) {
        return ESP_ERR_INVALID_STATE;
    }

    cancel_scan_timer();

#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    bt_mock_stop_scan();
    s_scan_active = bt_mock_is_scanning();
#else
    s_scan_active = false;
#endif

    return ESP_OK;
}

/* Check if scanning */
bool bt_is_scanning(void)
{
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    s_scan_active = bt_mock_is_scanning();
#endif
    return s_scan_active;
}

void cancel_scan_timer(void)
{
    if (s_scan_timer != NULL && xTimerIsTimerActive(s_scan_timer) != pdFALSE) {
        (void)xTimerStop(s_scan_timer, 0);
    }
}

static void scan_timeout_callback(TimerHandle_t timer)
{
    (void)timer;

    if (!s_scan_active) {
        return;
    }

    ESP_LOGI(TAG, "Mock: Scan timeout expired; stopping scan");

#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    bt_mock_stop_scan();
    s_scan_active = bt_mock_is_scanning();
#else
    s_scan_active = false;
#endif
}

#if !defined(BT_MOCK_PROVIDES_PROTOTYPES)
/**
 * Add a test device when the component mock does not provide an implementation.
 * The authoritative bt_mock component supplies this symbol when
 * BT_MOCK_PROVIDES_PROTOTYPES is defined, so avoid redefining the
 * function in that configuration to prevent linker conflicts.
 */
void bt_mock_add_test_device(const char* addr_str, const char* name, bt_device_type_t type)
{
    if (s_discovered_device_count >= MAX_DISCOVERED_DEVICES) {
        return; // No room for more devices
    }
    
    // Convert address string to byte array
    uint8_t addr[6];
    sscanf(addr_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
           &addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5]);
    
    // Check if device already exists
    for (int i = 0; i < s_discovered_device_count; i++) {
        if (memcmp(s_discovered_devices[i].addr, addr, 6) == 0) {
            // Device already exists - update name and type if needed
            strncpy(s_discovered_devices[i].name, name, sizeof(s_discovered_devices[0].name) - 1);
            
            // Set device type based on the type parameter
            if (type == BT_DEVICE_TYPE_AUDIO) {
                s_discovered_devices[i].cod = 0x240404; // Audio device
            } else {
                s_discovered_devices[i].cod = 0x120104; // Non-audio device
            }
            
            return;
        }
    }
    
    // Add the device to discovered devices list
    memcpy(s_discovered_devices[s_discovered_device_count].addr, addr, 6);
    strncpy(s_discovered_devices[s_discovered_device_count].name, name, sizeof(s_discovered_devices[0].name) - 1);
    s_discovered_devices[s_discovered_device_count].rssi = -70; // Default RSSI value
    
    // Set device type based on the type parameter
    if (type == BT_DEVICE_TYPE_AUDIO) {
        s_discovered_devices[s_discovered_device_count].cod = 0x240404; // Audio device
    } else {
        s_discovered_devices[s_discovered_device_count].cod = 0x120104; // Non-audio device
    }
    
    s_discovered_device_count++;
}
#endif // !BT_MOCK_PROVIDES_PROTOTYPES

/**
 * Store paired devices to persistent storage - Fix to actually store devices
 */
esp_err_t bt_store_paired_devices(void)
{
    if (!s_persistence_enabled) {
        return ESP_OK;
    }
    
    // Clear storage first
    s_stored_paired_device_count = 0;
    
    // Store all paired devices
    for (int i = 0; i < s_discovered_device_count; i++) {
        if (s_device_paired[i] && s_stored_paired_device_count < MAX_STORED_PAIRED_DEVICES) {
            memcpy(&s_stored_paired_devices[s_stored_paired_device_count], 
                   &s_discovered_devices[i], 
                   sizeof(bt_device_t));
            s_stored_paired_device_count++;
        }
    }
    
    return ESP_OK;
}

/**
 * Load paired devices from persistent storage - Fix to properly load stored devices
 */
esp_err_t bt_load_paired_devices(void)
{
    if (!s_persistence_enabled) {
        return ESP_OK;
    }
    
    // First, reset all pairing flags
    for (int i = 0; i < s_discovered_device_count; i++) {
        s_device_paired[i] = false;
    }
    s_paired_device_count = 0;
    
    // Add each stored device to the discovered list and mark as paired
    for (int i = 0; i < s_stored_paired_device_count; i++) {
        bool found = false;
        
        // Check if device already exists in discovered list
        for (int j = 0; j < s_discovered_device_count; j++) {
            if (memcmp(s_discovered_devices[j].addr, 
                      s_stored_paired_devices[i].addr, 
                      6) == 0) {
                // Device exists, mark as paired
                s_device_paired[j] = true;
                s_paired_device_count++;
                found = true;
                break;
            }
        }
        
        // If not found, add to discovered list
        if (!found && s_discovered_device_count < MAX_DISCOVERED_DEVICES) {
            memcpy(&s_discovered_devices[s_discovered_device_count], 
                  &s_stored_paired_devices[i], 
                  sizeof(bt_device_t));
            
            s_device_paired[s_discovered_device_count] = true;
            s_paired_device_count++;
            s_discovered_device_count++;
        }
    }
    
    return ESP_OK;
}

/**
 * Get paired device info - Fixed to return ESP_OK (0)
 */
esp_err_t bt_get_paired_device_info(const char* addr, bt_connection_info_t* info)
{
    if (!addr || !info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Convert address string to bytes
    uint8_t addr_bytes[6];
    if (sscanf(addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
           &addr_bytes[0], &addr_bytes[1], &addr_bytes[2], 
           &addr_bytes[3], &addr_bytes[4], &addr_bytes[5]) != 6) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find device
    for (int i = 0; i < s_discovered_device_count; i++) {
        if (memcmp(s_discovered_devices[i].addr, addr_bytes, 6) == 0 && s_device_paired[i]) {
            // Found it - fill in info
            memset(info, 0, sizeof(bt_connection_info_t));
            sprintf(info->addr, "%02x:%02x:%02x:%02x:%02x:%02x",  // Changed from remote_addr to addr
                    addr_bytes[0], addr_bytes[1], addr_bytes[2], 
                    addr_bytes[3], addr_bytes[4], addr_bytes[5]);
            strncpy(info->name, s_discovered_devices[i].name, sizeof(info->name) - 1);  // Changed from remote_name to name
            
            // Fix comparison with current connection
            info->connected = 
                strcasecmp(s_current_connection.addr, addr) == 0;  // Changed from remote_addr to addr
            
            // Remove profile member if it doesn't exist
            // info->profile = s_active_profile;  // Remove this line if profile isn't a member
            
            return ESP_OK;  // Return 0 to pass the test
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

/**
 * Unpair specific device - Fix to properly handle unpairing
 */
esp_err_t bt_unpair_device(const char* addr)
{
    if (!addr || !is_valid_mac_address(addr)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Convert address string to bytes for comparison
    uint8_t addr_bytes[6];
    if (sscanf(addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
           &addr_bytes[0], &addr_bytes[1], &addr_bytes[2], 
           &addr_bytes[3], &addr_bytes[4], &addr_bytes[5]) != 6) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find device and unpair it
    bool found = false;
    
    for (int i = 0; i < s_discovered_device_count; i++) {
        if (memcmp(s_discovered_devices[i].addr, addr_bytes, 6) == 0) {
            // If device is connected, disconnect it
            if (s_connected) {
                char dev_addr[18];
                sprintf(dev_addr, "%02x:%02x:%02x:%02x:%02x:%02x",
                      s_discovered_devices[i].addr[0], s_discovered_devices[i].addr[1],
                      s_discovered_devices[i].addr[2], s_discovered_devices[i].addr[3],
                      s_discovered_devices[i].addr[4], s_discovered_devices[i].addr[5]);
                
                if (strcasecmp(s_current_connection.addr, addr) == 0) {
                    s_connected = false;
                    s_streaming = false;
                    s_streaming_paused = false;
                    memset(&s_current_connection, 0, sizeof(s_current_connection));
                }
            }
            
            // Mark as unpaired and reduce count if needed
            if (s_device_paired[i]) {
                s_device_paired[i] = false;
                s_paired_device_count--;
            }
            
            found = true;
            break;
        }
    }
    
    bool unpaired = found;

#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    if (!unpaired) {
        esp_err_t mock_ret = bt_mock_unpair_device(addr);
        if (mock_ret == ESP_OK) {
            unpaired = true;
        } else if (mock_ret != ESP_ERR_NOT_FOUND) {
            return mock_ret;
        }
    }
#endif

    if (unpaired) {
        bt_store_paired_devices();
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

/**
 * Unpair all devices - Fix to correctly track unpaired device count
 */
esp_err_t bt_unpair_all_devices(void)
{
    ESP_LOGI(TAG, "Mock: Unpairing all devices");
    
    int unpaired_count = 0;
    
    // Disconnect connected device if any
    if (s_connected) {
        s_connected = false;
        s_streaming = false;
        s_streaming_paused = false;
        memset(&s_current_connection, 0, sizeof(s_current_connection));
    }
    
    // Count paired devices and unpair them
    for (int i = 0; i < s_discovered_device_count; i++) {
        if (s_device_paired[i]) {
            unpaired_count++;
            s_device_paired[i] = false;
        }
    }
    
    // Reset paired device count
    s_paired_device_count = 0;
    
    // Reset stored paired device list
    s_stored_paired_device_count = 0;

    // Also inform the component-level mock to clear its paired devices so
    // tests that use component helpers (bt_mock_*) remain consistent.
    esp_err_t comp_ret = ESP_OK;
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    comp_ret = bt_mock_unpair_all_devices();
#endif

    ESP_LOGI(TAG, "Mock: Unpaired %d devices", unpaired_count);

    // Store the empty paired device list
    bt_store_paired_devices();

    // If the component-level call failed, propagate the error; otherwise return ESP_OK
    return comp_ret;
}

/**
 * Configure auto reconnect behavior
 */
esp_err_t bt_set_auto_reconnect(bool enable)
{
    s_auto_reconnect_config.auto_reconnect_enabled = enable;
    return ESP_OK;
}

/**
 * Get auto reconnect configuration
 */
bool bt_is_auto_reconnect_enabled(void)
{
    return s_auto_reconnect_config.auto_reconnect_enabled;
}

/**
 * Check if a device is paired
 */
bool bt_is_device_paired(const char* addr)
{
    if (!addr) {
        return false;
    }
    
    // Convert address string to bytes for comparison
    uint8_t addr_bytes[6];
    if (sscanf(addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
            &addr_bytes[0], &addr_bytes[1], &addr_bytes[2], 
            &addr_bytes[3], &addr_bytes[4], &addr_bytes[5]) != 6) {
        return false;
    }
    
    // Look for the device in our discovered list
    for (int i = 0; i < s_discovered_device_count; i++) {
        if (memcmp(s_discovered_devices[i].addr, addr_bytes, 6) == 0) {
            return s_device_paired[i];  // Return paired status
        }
    }
    
    // If not found in this mock's discovered list, fall back to component-level
    // mock helper if available. This keeps the two mock implementations
    // consistent when tests manipulate paired devices via component helpers.
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    return bt_mock_is_device_paired(addr);
#else
    return false;
#endif
}
#ifdef CONFIG_BT_MOCK_TESTING


void bt_conn_test_set_reconnect_results(const esp_err_t *results, size_t len)
{
    if (results == NULL || len == 0U) {
        s_test_reconnect_results_len = 0;
        s_test_reconnect_results_idx = 0;
        return;
    }

    size_t capped_len = len;
    if (capped_len > (sizeof(s_test_reconnect_results) / sizeof(s_test_reconnect_results[0]))) {
        capped_len = sizeof(s_test_reconnect_results) / sizeof(s_test_reconnect_results[0]);
    }

    memset(s_test_reconnect_results, 0, sizeof(s_test_reconnect_results));
    memcpy(s_test_reconnect_results, results, capped_len * sizeof(results[0]));
    s_test_reconnect_results_len = capped_len;
    s_test_reconnect_results_idx = 0;
}
#endif // CONFIG_BT_MOCK_TESTING
#ifdef CONFIG_BT_MOCK_TESTING

void bt_conn_test_set_reconnect_delay_ms(uint32_t delay_ms)
{
    s_test_reconnect_delay_ms = delay_ms;
    s_test_reconnect_delay_overridden = true;
}
#endif // CONFIG_BT_MOCK_TESTING
#ifdef CONFIG_BT_MOCK_TESTING

void bt_conn_test_reset_state(void)
{
    s_test_reconnect_results_len = 0;
    s_test_reconnect_results_idx = 0;
    s_reconnect_attempts = 0;
    s_current_connection.retry_count = 0;
    s_test_reconnect_delay_overridden = false;
    s_test_reconnect_delay_ms = s_auto_reconnect_config.retry_interval_ms;
}
#endif // CONFIG_BT_MOCK_TESTING
#ifdef CONFIG_BT_MOCK_TESTING

uint8_t bt_connection_manager_get_reconnect_attempts_for_test(void)
{
    return s_reconnect_attempts;
}
#endif // CONFIG_BT_MOCK_TESTING
