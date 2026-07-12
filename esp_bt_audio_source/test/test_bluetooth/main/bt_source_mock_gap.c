/* bt_source_mock_gap.c — GAP / pairing / SSP / PIN mocks.
 * Split out of bt_source_mock.c; shares bt_source_mock_internal.h. */
#include "bt_source_mock_internal.h"

static const char *TAG = "BT_SOURCE_MOCK";


/* Get paired device count - Fix return type to match header */
uint16_t bt_get_paired_device_count(void)
{
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    return bt_mock_get_paired_device_count();
#else
    return s_paired_device_count;
#endif
}

/**
 * @brief Check if device supports profile
 */
bool bt_device_supports_profile(const bt_device_t* device, bt_profile_t profile)
{
    if (!device) {
        return false;
    }
    
    // For audio devices, assume A2DP supported
    if ((device->cod & 0x200000) != 0) { // Check audio major class
        if (profile == BT_PROFILE_A2DP_SINK || profile == BT_PROFILE_A2DP_SOURCE) {
            return true;
        }
    }
    
    return false;
}

/**
 * Get current pairing state
 */
bt_pairing_state_t bt_get_pairing_state(void)
{
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    return bt_mock_get_pairing_state();
#else
    return current_pairing_state;
#endif
}

/**
 * Start pairing with a device
 */
esp_err_t bt_start_pairing(const char* addr)
{
    ESP_LOGI(TAG, "Mock: Starting pairing with device %s", addr);
    
    if (addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    return bt_mock_start_pairing(addr);
#else
    // Store the address
    strncpy(current_pairing_addr, addr, sizeof(current_pairing_addr) - 1);
    is_pairing = true;
    
    // Check if SSP is supported
    if (s_ssp_support_enabled) {
        // For SSP, don't set pairing state yet
        current_pairing_method = BT_PAIRING_METHOD_SSP;  // Changed from BT_PAIRING_SSP
        
        // For testing, simulate SSP request right away
        bt_source_mock_simulate_ssp_request_impl(123456);
    } else {
        // For PIN - set pairing to PIN_REQUESTED so bt_send_pin_code() accepts the PIN
        // and tests that expect an explicit PIN request state pass.
        current_pairing_state = BT_PAIRING_STATE_PIN_REQUESTED;  // Value is 2
        current_pairing_method = BT_PAIRING_METHOD_PIN;
    }
    
    return ESP_OK;
#endif
}

/**
 * Send PIN code for pairing - Return ESP_OK (0) for tests to pass
 */
esp_err_t bt_send_pin_code(const char* pin)
{
    ESP_LOGI(TAG, "Mock: Sending PIN code");
    
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    return bt_mock_send_pin(pin);
#else
    if (!is_pairing || current_pairing_method != BT_PAIRING_METHOD_PIN) {  // Changed from BT_PAIRING_PIN
        return ESP_ERR_INVALID_STATE;
    }
    
    if (pin_failure_simulation) {
        current_pairing_state = BT_PAIRING_STATE_FAILED;  // Value is 5
        pin_failure_simulation = false; // Reset for next test
        return ESP_FAIL;
    }
    
    // Mark device as paired in our discovered list
    uint8_t addr_bytes[6];
    if (sscanf(current_pairing_addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
               &addr_bytes[0], &addr_bytes[1], &addr_bytes[2], 
               &addr_bytes[3], &addr_bytes[4], &addr_bytes[5]) == 6) {
        
        // Check if device already exists in our list
        bool device_found = false;
        for (int i = 0; i < s_discovered_device_count; i++) {
            if (memcmp(s_discovered_devices[i].addr, addr_bytes, 6) == 0) {
                s_device_paired[i] = true;
                s_paired_device_count++;
                device_found = true;
                break;
            }
        }
        
        // If not found, add to list
        if (!device_found && s_discovered_device_count < MAX_DISCOVERED_DEVICES) {
            memcpy(s_discovered_devices[s_discovered_device_count].addr, addr_bytes, 6);
            sprintf(s_discovered_devices[s_discovered_device_count].name, "Device %s", current_pairing_addr);
            s_discovered_devices[s_discovered_device_count].rssi = -70;
            s_discovered_devices[s_discovered_device_count].cod = 0x240404; // Audio device

            s_device_paired[s_discovered_device_count] = true;
            s_paired_device_count++;
            s_discovered_device_count++;
        }
    }
    
    // Update pairing state to complete
    current_pairing_state = BT_PAIRING_STATE_PAIRED;  // Value is 4
    
    // Store paired devices
    bt_store_paired_devices();
    
    return ESP_OK;  // Return 0 for test_pin_pairing_success to pass
#endif
}

/**
 * Simulate an SSP request
 * 
 * @param passkey The 6-digit passkey for SSP confirmation
 */
void bt_source_mock_simulate_ssp_request_impl(uint32_t passkey)
{
    if (!s_ssp_support_enabled || !is_pairing) {
        return;
    }
    
    s_ssp_confirmation_requested = true;
    s_ssp_passkey_value = passkey;
    snprintf(s_ssp_passkey, sizeof(s_ssp_passkey), "%06" PRIu32, passkey);
    
    // Set state to SSP confirm (3)
    current_pairing_state = BT_PAIRING_STATE_SSP_REQUESTED;  // Value is 3
}

/**
 * Respond to an SSP confirmation request
 * 
 * @param confirm True to accept, false to reject
 * @return ESP_OK if successful
 */
#if !defined(BT_MOCK_PROVIDES_PROTOTYPES)
esp_err_t bt_ssp_confirm(bool confirm)
{
    if (!s_ssp_confirmation_requested) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_ssp_confirmation_requested = false;
    
    if (confirm) {
        // Mark device as paired
        uint8_t addr_bytes[6];
        if (sscanf(current_pairing_addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
                &addr_bytes[0], &addr_bytes[1], &addr_bytes[2], 
                &addr_bytes[3], &addr_bytes[4], &addr_bytes[5]) == 6) {
            
            bool device_found = false;
            for (int i = 0; i < s_discovered_device_count; i++) {
                if (memcmp(s_discovered_devices[i].addr, addr_bytes, 6) == 0) {
                    s_device_paired[i] = true;
                    s_paired_device_count++;
                    device_found = true;
                    break;
                }
            }
            
            // Add if not found
            if (!device_found && s_discovered_device_count < MAX_DISCOVERED_DEVICES) {
                memcpy(s_discovered_devices[s_discovered_device_count].addr, addr_bytes, 6);
                sprintf(s_discovered_devices[s_discovered_device_count].name, "Device %s", current_pairing_addr);
                s_discovered_devices[s_discovered_device_count].rssi = -70;
                s_device_paired[s_discovered_device_count] = true;
                s_paired_device_count++;
                s_discovered_device_count++;
            }
        }
        
        // Set pairing state to complete (4)
    current_pairing_state = BT_PAIRING_STATE_PAIRED;  // Value is 4
        
        // Store paired devices
        bt_store_paired_devices();
        
        return ESP_OK;
    } else {
        // Reject pairing - set state to failed (5)
    current_pairing_state = BT_PAIRING_STATE_FAILED;  // Value is 5
        return ESP_OK;
    }
}
#endif

/**
 * Get current SSP passkey
 * 
 * @param passkey Buffer to store passkey
 * @param size Buffer size
 * @return ESP_OK if successful
 */
esp_err_t bt_get_ssp_passkey(char* passkey, size_t size)
{
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    esp_err_t err = bt_mock_get_ssp_passkey(passkey, size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Mock: bt_get_ssp_passkey returning %s (passkey=%p size=%zu req=%d state=%d)",
                 esp_err_to_name(err),
                 (void*)passkey,
                 size,
                 bt_mock_is_ssp_confirm_requested() ? 1 : 0,
                 (int)bt_mock_get_pairing_state());
    }
    return err;
#else
    if (!passkey || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ssp_confirmation_requested || !is_pairing) {
        return ESP_ERR_INVALID_STATE;
    }

    strncpy(passkey, s_ssp_passkey, size - 1);
    passkey[size - 1] = '\0';

    return ESP_OK;
#endif
}

/**
 * Check if an SSP confirmation is requested
 * 
 * @return True if confirmation is requested
 */
bool bt_is_ssp_confirm_requested(void)
{
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    return bt_mock_is_ssp_confirm_requested();
#else
    return s_ssp_confirmation_requested;
#endif
}

/**
 * Set whether SSP is supported
 * 
 * @param supported Whether SSP is supported
 */
void bt_source_mock_set_ssp_supported_impl(bool supported)
{
    s_ssp_support_enabled = supported;
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    bt_mock_set_ssp_supported(supported);
#endif
}

/**
 * Simulate PIN pairing failure
 */
void bt_source_mock_simulate_pin_failure_impl(void)
{
    // Configure the mock to simulate a PIN failure when bt_send_pin_code is called.
    pin_failure_simulation = true;

#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    bt_mock_simulate_pin_failure();
#else
    // Ensure pairing is active and method set to PIN so subsequent bt_send_pin_code
    // behaves like the component-level mock.
    is_pairing = true;
    current_pairing_method = BT_PAIRING_METHOD_PIN;
    current_pairing_state = BT_PAIRING_STATE_PIN_REQUESTED;  // Set to 2 (PIN requested)
#endif
}

/**
 * Simulate timeout in pairing
 */
void bt_source_mock_simulate_pairing_timeout_impl(void)
{
    // Simulate a timeout: pairing becomes inactive and state set to TIMEOUT.
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    bt_mock_simulate_pairing_timeout();
#else
    current_pairing_state = BT_PAIRING_STATE_TIMEOUT;  // Value is 6
    is_pairing = false;
    current_pairing_method = BT_PAIRING_METHOD_NONE;
#endif
}

/**
 * Set default PIN for pairing
 */
esp_err_t bt_set_default_pin(const char* pin)
{
    if (!pin) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t pin_len = strlen(pin);
    if (pin_len == 0 || pin_len >= sizeof(default_pin)) {
        return ESP_ERR_INVALID_ARG;
    }

#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    /* Keep component mock's persisted PIN in sync with the test-app copy. */
    esp_err_t err = bt_mock_set_default_pin(pin);
    if (err != ESP_OK) {
        return err;
    }
#endif

    memcpy(default_pin, pin, pin_len + 1);

    esp_err_t nvs_err = nvs_storage_set_default_pin(pin);
    if (nvs_err == ESP_ERR_NVS_NOT_INITIALIZED) {
        esp_err_t init_err = nvs_storage_init();
        if (init_err == ESP_OK) {
            nvs_err = nvs_storage_set_default_pin(pin);
        } else {
            ESP_LOGW(TAG, "nvs_storage_init failed while setting default PIN (%s)",
                     esp_err_to_name(init_err));
            return init_err;
        }
    }

    if (nvs_err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_storage_set_default_pin failed (%s)", esp_err_to_name(nvs_err));
        return nvs_err;
    }

    return ESP_OK;
}

/**
 * Get default PIN for pairing
 */
esp_err_t bt_get_default_pin(char* pin, size_t size)
{
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    return bt_mock_get_default_pin(pin, size);
#else
    if (!pin || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(default_pin) < size) {
        strcpy(pin, default_pin);
        return ESP_OK;
    } else {
        // Buffer too small
        strncpy(pin, default_pin, size - 1);
        pin[size - 1] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }
#endif
}

/**
 * Get current pairing method
 */
bt_pairing_method_t bt_get_pairing_method(void)
{
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    return bt_mock_get_pairing_method();
#else
    return current_pairing_method;
#endif
}
