/* bt_source_stubs_gap.c — GAP / pairing / SSP / PIN + unpair stubs.
 * Split out of bt_source_stubs.c; shares bt_source_stubs_internal.h. */
#include "bt_source_stubs_internal.h"

static const char *TAG = "BT_SOURCE_STUB";


/**
 * @brief Simulate failure during PIN pairing
 */
BT_WEAK_FN void bt_mock_simulate_pin_failure(void)
{
    s_simulate_pairing_failure = true;
    ESP_LOGI(TAG, "Simulating PIN failure on next pairing attempt");
}

/**
 * @brief Simulate pairing timeout
 */
BT_WEAK_FN void bt_mock_simulate_pairing_timeout(void)
{
    s_simulate_pairing_timeout = true;
    ESP_LOGI(TAG, "Simulating pairing timeout on next pairing attempt");
}

/**
 * @brief Set whether SSP is supported
 */
BT_WEAK_FN void bt_mock_set_ssp_supported(bool supported)
{
    s_ssp_supported = supported;
    ESP_LOGI(TAG, "SSP support set to: %s", supported ? "true" : "false");
}

/**
 * @brief Check if a device supports a given profile
 */
BT_WEAK_FN bool bt_device_supports_profile(const bt_device_t* device, bt_profile_t profile)
{
    if (device == NULL) {
        return false;
    }
    
    /* For the stub, all devices support A2DP source and sink */
    if (profile == BT_PROFILE_A2DP_SOURCE || profile == BT_PROFILE_A2DP_SINK) {
        /* Check if it's an audio device */
        return ((device->cod & 0x200000) != 0);
    }
    
    return false;
}

/* 
 * Pairing Functions
 */

/**
 * @brief Start Bluetooth pairing with a device
 */
BT_WEAK_FN esp_err_t bt_start_pairing(const char* addr)
{
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    return bt_mock_start_pairing(addr);
#else
    if (!s_is_connected) {
        ESP_LOGE(TAG, "Not connected to any device");
        return ESP_ERR_INVALID_STATE;
    }

    /* Update state */
    s_pairing_state = BT_PAIRING_STATE_STARTED;

    /* Check if we should simulate failure or timeout */
    if (s_simulate_pairing_failure) {
        vTaskDelay(pdMS_TO_TICKS(500));
        s_pairing_state = BT_PAIRING_STATE_FAILED;
        s_simulate_pairing_failure = false;
        ESP_LOGI(TAG, "Simulated pairing failure");
        return ESP_OK;
    }

    if (s_simulate_pairing_timeout) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        s_pairing_state = BT_PAIRING_STATE_TIMEOUT;
        s_simulate_pairing_timeout = false;
        ESP_LOGI(TAG, "Simulated pairing timeout");
        return ESP_OK;
    }

    /* Determine pairing method */
    if (s_ssp_supported) {
        s_pairing_method = BT_PAIRING_METHOD_SSP;
        s_pairing_state = BT_PAIRING_STATE_SSP_REQUESTED;
        s_stub_ssp_confirmation_requested = true;
        /* Generate a random passkey */
        s_passkey = 100000 + (rand() % 900000);
        sprintf(s_stub_ssp_passkey, "%06" PRIu32, s_passkey);
        ESP_LOGI(TAG, "SSP pairing started, passkey: %s", s_stub_ssp_passkey);
    } else {
        s_pairing_method = BT_PAIRING_METHOD_PIN;
        s_pairing_state = BT_PAIRING_STATE_PIN_REQUESTED;
        ESP_LOGI(TAG, "PIN pairing started");
    }

    return ESP_OK;
#endif
}

/**
 * @brief Send PIN code for pairing
 */
BT_WEAK_FN esp_err_t bt_send_pin_code(const char* pin)
{
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    return bt_mock_send_pin(pin);
#else
    /* Accept PIN when the pairing state indicates a PIN request or when
     * pairing has just started but the implementation treats STARTED as an
     * acceptable moment to provide a PIN. Some component mocks use
        return ESP_ERR_NOT_FOUND;
     * permissive here to match component behavior.
     */
    if (s_pairing_state != BT_PAIRING_STATE_PIN_REQUESTED &&
        s_pairing_state != BT_PAIRING_STATE_STARTED) {
        ESP_LOGE(TAG, "PIN not requested");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "PIN code sent: %s", pin);

    /* For testing, compare with default PIN */
    if (strcmp(pin, s_default_pin) == 0) {
        s_pairing_state = BT_PAIRING_STATE_PAIRED;

        /* Add to paired devices */
        for (int i = 0; i < s_device_count; i++) {
            char dev_addr[18];
            sprintf(dev_addr, "%02x:%02x:%02x:%02x:%02x:%02x",
                    s_devices[i].addr[0], s_devices[i].addr[1], s_devices[i].addr[2],
                    s_devices[i].addr[3], s_devices[i].addr[4], s_devices[i].addr[5]);

            if (strcasecmp(dev_addr, s_connected_device_addr) == 0 && s_stub_paired_device_count < MAX_PAIRED_DEVICES) {
                memcpy(&s_paired_devices[s_stub_paired_device_count++], &s_devices[i], sizeof(bt_device_t));
                s_paired_devices[s_stub_paired_device_count - 1].paired = true;
                break;
            }
        }

        ESP_LOGI(TAG, "Pairing successful");
    } else {
        s_pairing_state = BT_PAIRING_STATE_FAILED;
        ESP_LOGI(TAG, "Pairing failed: incorrect PIN");
    }

    return ESP_OK;
#endif
}

/**
 * @brief Get current pairing state
 */
BT_WEAK_FN bt_pairing_state_t bt_get_pairing_state(void)
{
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    return bt_mock_get_pairing_state();
#else
    return s_pairing_state;
#endif
}

/**
 * @brief Check if a device is paired
 */
BT_WEAK_FN bool bt_is_device_paired(const char* addr)
{
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    return bt_mock_is_device_paired(addr);
#else
    for (int i = 0; i < s_stub_paired_device_count; i++) {
        char dev_addr[18];
        sprintf(dev_addr, "%02x:%02x:%02x:%02x:%02x:%02x",
                s_paired_devices[i].addr[0], s_paired_devices[i].addr[1], s_paired_devices[i].addr[2],
                s_paired_devices[i].addr[3], s_paired_devices[i].addr[4], s_paired_devices[i].addr[5]);

        if (strcasecmp(dev_addr, addr) == 0) {
            return true;
        }
    }

    return false;
#endif
}

/**
 * @brief Set default PIN code
 */
BT_WEAK_FN esp_err_t bt_set_default_pin(const char* pin)
{
    if (pin == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    return bt_mock_set_default_pin(pin);
#else
    strncpy(s_default_pin, pin, sizeof(s_default_pin) - 1);
    s_default_pin[sizeof(s_default_pin) - 1] = '\0';
    ESP_LOGI(TAG, "Default PIN set to: %s", s_default_pin);
    return ESP_OK;
#endif
}

/**
 * @brief Get default PIN code
 */
BT_WEAK_FN esp_err_t bt_get_default_pin(char* pin, size_t size)
{
    if (pin == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    return bt_mock_get_default_pin(pin, size);
#else
    strncpy(pin, s_default_pin, size - 1);
    pin[size - 1] = '\0';
    return ESP_OK;
#endif
}

/**
 * @brief Respond to a SSP confirmation request
 */
BT_WEAK_FN esp_err_t bt_ssp_confirm(bool confirm)
{
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    return bt_mock_confirm_ssp(confirm);
#else
    if (!s_stub_ssp_confirmation_requested) {
        ESP_LOGE(TAG, "No SSP confirmation requested");
        return ESP_ERR_INVALID_STATE;
    }

    s_stub_ssp_confirmation_requested = false;

    if (confirm) {
        s_pairing_state = BT_PAIRING_STATE_PAIRED;

        /* Add to paired devices */
        for (int i = 0; i < s_device_count; i++) {
            char dev_addr[18];
            sprintf(dev_addr, "%02x:%02x:%02x:%02x:%02x:%02x",
                    s_devices[i].addr[0], s_devices[i].addr[1], s_devices[i].addr[2],
                    s_devices[i].addr[3], s_devices[i].addr[4], s_devices[i].addr[5]);

            if (strcasecmp(dev_addr, s_connected_device_addr) == 0 && s_stub_paired_device_count < MAX_PAIRED_DEVICES) {
                memcpy(&s_paired_devices[s_stub_paired_device_count++], &s_devices[i], sizeof(bt_device_t));
                s_paired_devices[s_stub_paired_device_count - 1].paired = true;
                break;
            }
        }

        ESP_LOGI(TAG, "SSP confirmation accepted, pairing successful");
    } else {
        s_pairing_state = BT_PAIRING_STATE_FAILED;
        ESP_LOGI(TAG, "SSP confirmation rejected, pairing failed");
    }

    return ESP_OK;
#endif
}

/**
 * @brief Get the current SSP passkey
 */
BT_WEAK_FN esp_err_t bt_get_ssp_passkey(char* passkey, size_t size)
{
    if (passkey == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    /* Delegate to component-level mock which holds authoritative SSP passkey */
    return bt_mock_get_ssp_passkey(passkey, size);
#else
    if (!s_stub_ssp_confirmation_requested) {
        return ESP_ERR_NOT_FOUND;
    }
    
    strncpy(passkey, s_stub_ssp_passkey, size - 1);
    passkey[size - 1] = '\0';
    
    return ESP_OK;
#endif
}

/**
 * @brief Check if SSP confirmation is requested
 */
BT_WEAK_FN bool bt_is_ssp_confirm_requested(void)
{
    return s_stub_ssp_confirmation_requested;
}

/**
 * @brief Get current pairing method
 */
BT_WEAK_FN bt_pairing_method_t bt_get_pairing_method(void)
{
    return s_pairing_method;
}

/**
 * @brief Unpair a specific device
 */
BT_WEAK_FN esp_err_t bt_unpair_device(const char* addr)
{
    if (addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    /* Delegate unpair operation to component-level authoritative mock */
    esp_err_t err = bt_mock_unpair_device(addr);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Unpaired device: %s (delegated to mock)", addr);
    }
    return err;
#else
    bool found = false;
    int found_index = -1;
    
    /* Find the device by address */
    for (int i = 0; i < s_stub_paired_device_count; i++) {
        char dev_addr[18];
        sprintf(dev_addr, "%02x:%02x:%02x:%02x:%02x:%02x",
                s_paired_devices[i].addr[0], s_paired_devices[i].addr[1], s_paired_devices[i].addr[2],
                s_paired_devices[i].addr[3], s_paired_devices[i].addr[4], s_paired_devices[i].addr[5]);
        
        if (strcasecmp(dev_addr, addr) == 0) {
            found = true;
            found_index = i;
            break;
        }
    }
    
    if (!found) {
        return ESP_ERR_NOT_FOUND;
    }
    
    /* Remove by shifting remaining devices */
    for (int i = found_index; i < s_stub_paired_device_count - 1; i++) {
        memcpy(&s_paired_devices[i], &s_paired_devices[i + 1], sizeof(bt_device_t));
    }
    
    s_stub_paired_device_count--;
    ESP_LOGI(TAG, "Unpaired device: %s", addr);
    
    return ESP_OK;
#endif
}

/**
 * @brief Unpair all devices
 */
BT_WEAK_FN esp_err_t bt_unpair_all_devices(void)
{
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    return bt_mock_unpair_all_devices();
#else
    int count_before = s_stub_paired_device_count;
    s_stub_paired_device_count = 0;
    ESP_LOGI(TAG, "Unpaired all devices (%d)", count_before);
    return ESP_OK;
#endif
}

/**
 * @brief Get paired device count
 */
BT_WEAK_FN uint16_t bt_get_paired_device_count(void)
{
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    /* Delegate to component-provided authoritative mock */
    return bt_mock_get_paired_device_count();
#else
    return s_stub_paired_device_count;
#endif
}
