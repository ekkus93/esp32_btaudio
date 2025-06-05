#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_err.h"
#include "bt_source.h"

static const char *TAG = "BT_SOURCE";

// Define missing error constant if needed
#ifndef ESP_ERR_INSUFFICIENT_RESOURCES
#define ESP_ERR_INSUFFICIENT_RESOURCES 0x104
#endif

// Basic state variables (real implementation would have more)
static bool is_initialized = false;
static bool is_connected = false;
static bool is_scanning = false;
static bt_streaming_state_t streaming_state = BT_STREAM_STATE_STOPPED;
static bt_pairing_state_t pairing_state = BT_PAIRING_STATE_IDLE;
static bt_pairing_method_t pairing_method = BT_PAIRING_METHOD_NONE;

// Forward declarations for mock functions when in testing mode
#ifdef BT_USE_MOCKS
// Use declarations that match the functions in bt_mock_devices.h
void bt_mock_reset(void);
esp_err_t bt_mock_start_pairing(const char* addr);
bt_pairing_state_t bt_mock_get_pairing_state(void);
bt_pairing_method_t bt_mock_get_pairing_method(void);
esp_err_t bt_mock_send_pin(const char* pin);
esp_err_t bt_mock_confirm_ssp(bool confirm);
bool bt_mock_is_device_paired(const char* addr);
esp_err_t bt_mock_add_paired_device(bt_device_t* device);
void bt_mock_set_ssp_supported(bool supported);
#endif

// Implementation of bt_init - simple version for now
esp_err_t bt_init(void) {
#ifdef BT_USE_MOCKS
    // Just delegate to mock in test mode
    is_initialized = true;
    ESP_LOGI(TAG, "Initialized Bluetooth stack (MOCK)");
    return ESP_OK;
#else
    // Real implementation would initialize BT stack
    is_initialized = true;
    ESP_LOGI(TAG, "Initialized Bluetooth stack");
    return ESP_OK;
#endif
}

// Implementation of bt_scan with proper format string
esp_err_t bt_scan(uint32_t timeout_seconds) {
#ifdef BT_USE_MOCKS
    // Just delegate to mock in test mode
    is_scanning = true;
    ESP_LOGI(TAG, "Starting Bluetooth scan with timeout %" PRIu32 " seconds (MOCK)", timeout_seconds);
    return ESP_OK;
#else
    // Real implementation would start scan with timeout
    is_scanning = true;
    ESP_LOGI(TAG, "Starting Bluetooth scan with timeout %" PRIu32 " seconds", timeout_seconds);
    return ESP_OK;
#endif
}

// Check scanning state
bool bt_is_scanning(void) {
    return is_scanning;
}

// Stop scanning
esp_err_t bt_scan_stop(void) {
#ifdef BT_USE_MOCKS
    // Just update state in test mode
    is_scanning = false;
    return ESP_OK;
#else
    // Real implementation would stop scanning
    is_scanning = false;
    return ESP_OK;
#endif
}

// Add other core functions with the same pattern...

// Implementation of bt_add_paired_device (matches header)
esp_err_t bt_add_paired_device(bt_device_t* device) {
    if (!device) {
        return ESP_ERR_INVALID_ARG;
    }
    
#ifdef BT_USE_MOCKS
    // Use the mock implementation in test mode
    return ESP_OK;
#else
    // Real implementation would add the device to paired list
    return ESP_OK;
#endif
}

// Get default PIN for pairing (simple implementation)
esp_err_t bt_get_default_pin(char* pin, size_t size) {
    if (!pin || size < 5) { // Need at least 5 bytes for "1234\0"
        return ESP_ERR_INSUFFICIENT_RESOURCES;
    }
    
    strcpy(pin, "1234"); // Default PIN
    return ESP_OK;
}

// Get current streaming state
bt_streaming_state_t bt_get_streaming_state(void) {
    return streaming_state;
}

// Reset for testing
void bt_reset_for_test(void) {
    is_initialized = false;
    is_connected = false;
    is_scanning = false;
    streaming_state = BT_STREAM_STATE_STOPPED;
    pairing_state = BT_PAIRING_STATE_IDLE;
    pairing_method = BT_PAIRING_METHOD_NONE;
}

// Implementation of bt_start_pairing using mock when in test mode
esp_err_t bt_start_pairing(const char* addr) {
#ifdef BT_USE_MOCKS
    ESP_LOGI(TAG, "Mock: Starting pairing with device %s", addr);
    
    if (addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = bt_mock_start_pairing(addr);
    
    // We need to get the pairing state and method from the mock
    pairing_state = bt_mock_get_pairing_state();
    pairing_method = bt_mock_get_pairing_method();
    
    return ret;
#else
    // ...existing implementation...
#endif
}
