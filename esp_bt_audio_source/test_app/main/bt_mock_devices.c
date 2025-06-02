/**
 * Minimal mock implementation for Bluetooth device testing
 * Only used during tests - not part of production code
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h> // Add this for PRIu32
#include "esp_log.h"
#include "bt_mock_devices.h"

static const char *TAG = "BT_MOCK";

// Mock device storage
#define MAX_MOCK_DEVICES 10
static bt_mock_device_t mock_devices[MAX_MOCK_DEVICES];
static int mock_device_count = 0;
static bool mock_initialized = false;
static bool mock_is_scanning = false;
static bool mock_is_connected = false;
static bool mock_is_streaming = false;
static bool mock_is_paused = false;
static char mock_connected_device[18] = {0};
static bt_mock_callbacks_t callbacks = {0};

// Pairing state tracking
static bt_pairing_state_t current_pairing_state = BT_PAIRING_STATE_NONE;
static bt_pairing_method_t current_pairing_method = BT_PAIRING_NONE;
static bool ssp_supported = true;
static bool ssp_confirm_requested = false;
static char ssp_passkey[7] = {0};
static char default_pin[16] = "1234";
static bool pin_failure_simulated = false;
static bool pairing_timeout_simulated = false;

/**
 * Initialize the mock device system
 */
void bt_mock_init(void) {
    memset(mock_devices, 0, sizeof(mock_devices));
    mock_device_count = 0;
    mock_initialized = true;
    mock_is_scanning = false;
    mock_is_connected = false;
    mock_is_streaming = false;
    mock_is_paused = false;
    memset(mock_connected_device, 0, sizeof(mock_connected_device));
    
    // Reset pairing state
    current_pairing_state = BT_PAIRING_STATE_NONE;
    current_pairing_method = BT_PAIRING_NONE;
    ssp_supported = true;
    ssp_confirm_requested = false;
    memset(ssp_passkey, 0, sizeof(ssp_passkey));
    strcpy(default_pin, "1234");
    pin_failure_simulated = false;
    pairing_timeout_simulated = false;
    
    ESP_LOGI(TAG, "Mock Bluetooth system initialized");
}

/**
 * Reset all mock state
 */
void bt_mock_reset(void) {
    mock_device_count = 0;
    mock_is_scanning = false;
    mock_is_connected = false;
    mock_is_streaming = false;
    mock_is_paused = false;
    memset(mock_connected_device, 0, sizeof(mock_connected_device));
    
    // Reset pairing state
    current_pairing_state = BT_PAIRING_STATE_NONE;
    current_pairing_method = BT_PAIRING_NONE;
    ssp_confirm_requested = false;
    memset(ssp_passkey, 0, sizeof(ssp_passkey));
    pin_failure_simulated = false;
    pairing_timeout_simulated = false;
    
    ESP_LOGI(TAG, "Mock Bluetooth state reset");
}

/**
 * Register callbacks for mock events
 */
void bt_mock_register_callbacks(bt_mock_callbacks_t *cb) {
    if (cb) {
        callbacks = *cb;
    } else {
        memset(&callbacks, 0, sizeof(callbacks));
    }
}

/**
 * Add a mock device for testing
 */
esp_err_t bt_mock_add_device(const char* addr, const char* name, 
                       bt_device_type_t type, bool paired) {
    if (mock_device_count >= MAX_MOCK_DEVICES) {
        return ESP_ERR_NO_MEM;
    }
    
    // Parse address
    uint8_t addr_bytes[6];
    if (sscanf(addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
               &addr_bytes[0], &addr_bytes[1], &addr_bytes[2], 
               &addr_bytes[3], &addr_bytes[4], &addr_bytes[5]) != 6) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Add device to mock devices
    memcpy(mock_devices[mock_device_count].addr, addr_bytes, 6);
    strncpy(mock_devices[mock_device_count].name, name, sizeof(mock_devices[mock_device_count].name) - 1);
    mock_devices[mock_device_count].name[sizeof(mock_devices[mock_device_count].name) - 1] = '\0';
    mock_devices[mock_device_count].type = type;
    mock_devices[mock_device_count].paired = paired;
    
    // Set device class based on type
    switch (type) {
        case BT_DEVICE_TYPE_AUDIO:
            mock_devices[mock_device_count].cod = 0x240404; // Audio
            break;
        case BT_DEVICE_TYPE_PHONE:
            mock_devices[mock_device_count].cod = 0x200412; // Phone
            break;
        case BT_DEVICE_TYPE_HID:
            mock_devices[mock_device_count].cod = 0x250000; // HID
            break;
        default:
            mock_devices[mock_device_count].cod = 0x000000; // Unknown
            break;
    }
    
    mock_device_count++;
    ESP_LOGI(TAG, "Added mock device: %s (%s)", addr, name);
    return ESP_OK;
}

/**
 * Start mock discovery process
 */
esp_err_t bt_mock_start_scan(void) {
    mock_is_scanning = true;
    ESP_LOGI(TAG, "Mock Bluetooth scan started");
    
    // Notify about existing mock devices if callback registered
    if (callbacks.device_found) {
        for (int i = 0; i < mock_device_count; i++) {
            bt_device_t device;
            memcpy(device.addr, mock_devices[i].addr, 6);
            strcpy(device.name, mock_devices[i].name);
            device.rssi = -60 - (rand() % 40);  // Random RSSI between -60 and -99
            device.cod = mock_devices[i].cod;
            callbacks.device_found(&device, callbacks.user_data);
        }
    }
    
    return ESP_OK;
}

/**
 * Stop mock discovery
 */
esp_err_t bt_mock_stop_scan(void) {
    mock_is_scanning = false;
    ESP_LOGI(TAG, "Mock Bluetooth scan stopped");
    return ESP_OK;
}

/**
 * Check if mock scanning is active
 */
bool bt_mock_is_scanning(void) {
    return mock_is_scanning;
}

/**
 * Get mock discovered devices
 */
int bt_mock_get_devices(bt_device_t *devices, int max_count) {
    if (!devices || max_count <= 0) {
        return 0;
    }
    
    int count = (mock_device_count < max_count) ? mock_device_count : max_count;
    
    for (int i = 0; i < count; i++) {
        memcpy(devices[i].addr, mock_devices[i].addr, 6);
        strcpy(devices[i].name, mock_devices[i].name);
        devices[i].rssi = -60 - (rand() % 40);  // Random RSSI between -60 and -99
        devices[i].cod = mock_devices[i].cod;
        devices[i].paired = mock_devices[i].paired;
    }
    
    return count;
}

/**
 * Connect to mock device
 */
esp_err_t bt_mock_connect(const char* addr) {
    // Find device in mock list
    for (int i = 0; i < mock_device_count; i++) {
        char dev_addr[18];
        sprintf(dev_addr, "%02x:%02x:%02x:%02x:%02x:%02x",
                mock_devices[i].addr[0], mock_devices[i].addr[1],
                mock_devices[i].addr[2], mock_devices[i].addr[3],
                mock_devices[i].addr[4], mock_devices[i].addr[5]);
                
        if (strcasecmp(dev_addr, addr) == 0) {
            // Found the device - connect
            mock_is_connected = true;
            strcpy(mock_connected_device, addr);
            ESP_LOGI(TAG, "Mock connected to device: %s", addr);
            
            // Notify connection if callback registered
            if (callbacks.connection_change) {
                bt_device_t device;
                memcpy(device.addr, mock_devices[i].addr, 6);
                strcpy(device.name, mock_devices[i].name);
                device.rssi = -60 - (rand() % 40);
                device.cod = mock_devices[i].cod;
                callbacks.connection_change(true, &device, ESP_OK, callbacks.user_data);
            }
            
            return ESP_OK;
        }
    }
    
    ESP_LOGW(TAG, "Mock device not found: %s", addr);
    return ESP_ERR_NOT_FOUND;
}

/**
 * Disconnect mock device
 */
esp_err_t bt_mock_disconnect(void) {
    if (!mock_is_connected) {
        return ESP_OK;  // Already disconnected
    }
    
    // Stop streaming if active
    if (mock_is_streaming) {
        mock_is_streaming = false;
        mock_is_paused = false;
    }
    
    mock_is_connected = false;
    ESP_LOGI(TAG, "Mock disconnected from device: %s", mock_connected_device);
    
    // Notify disconnection if callback registered
    if (callbacks.connection_change) {
        callbacks.connection_change(false, NULL, ESP_OK, callbacks.user_data);
    }
    
    memset(mock_connected_device, 0, sizeof(mock_connected_device));
    return ESP_OK;
}

/**
 * Check if mock is connected
 */
bool bt_mock_is_connected(void) {
    return mock_is_connected;
}

/**
 * Get paired mock devices 
 */
int bt_mock_get_paired_devices(bt_device_t *devices, int max_count) {
    if (!devices || max_count <= 0) {
        return 0;
    }
    
    int paired_count = 0;
    
    // Copy only paired devices
    for (int i = 0; i < mock_device_count && paired_count < max_count; i++) {
        if (mock_devices[i].paired) {
            memcpy(devices[paired_count].addr, mock_devices[i].addr, 6);
            strcpy(devices[paired_count].name, mock_devices[i].name);
            devices[paired_count].rssi = -60 - (rand() % 40);
            devices[paired_count].cod = mock_devices[i].cod;
            devices[paired_count].paired = true;
            paired_count++;
        }
    }
    
    return paired_count;
}

/**
 * Check if a mock device is paired
 */
bool bt_mock_is_device_paired(const char* addr) {
    if (!addr) return false;
    
    // Find device in mock list
    for (int i = 0; i < mock_device_count; i++) {
        char dev_addr[18];
        sprintf(dev_addr, "%02x:%02x:%02x:%02x:%02x:%02x",
                mock_devices[i].addr[0], mock_devices[i].addr[1],
                mock_devices[i].addr[2], mock_devices[i].addr[3],
                mock_devices[i].addr[4], mock_devices[i].addr[5]);
                
        if (strcasecmp(dev_addr, addr) == 0) {
            return mock_devices[i].paired;
        }
    }
    
    return false;
}

/**
 * Start mock streaming
 */
esp_err_t bt_mock_start_streaming(void) {
    if (!mock_is_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    
    mock_is_streaming = true;
    mock_is_paused = false;
    ESP_LOGI(TAG, "Mock streaming started");
    return ESP_OK;
}

/**
 * Stop mock streaming
 */
esp_err_t bt_mock_stop_streaming(void) {
    mock_is_streaming = false;
    mock_is_paused = false;
    ESP_LOGI(TAG, "Mock streaming stopped");
    return ESP_OK;
}

/**
 * Pause mock streaming
 */
esp_err_t bt_mock_pause_streaming(void) {
    if (!mock_is_streaming) {
        return ESP_ERR_INVALID_STATE;
    }
    
    mock_is_paused = true;
    ESP_LOGI(TAG, "Mock streaming paused");
    return ESP_OK;
}

/**
 * Resume mock streaming
 */
esp_err_t bt_mock_resume_streaming(void) {
    if (!mock_is_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    
    mock_is_streaming = true;
    mock_is_paused = false;
    ESP_LOGI(TAG, "Mock streaming resumed");
    return ESP_OK;
}

/**
 * Check if mock is streaming
 */
bool bt_mock_is_streaming(void) {
    return mock_is_streaming;
}

/**
 * Check if mock streaming is paused
 */
bool bt_mock_is_paused(void) {
    return mock_is_paused;
}

/**
 * Get mock streaming state
 */
bt_streaming_state_t bt_mock_get_streaming_state(void) {
    if (!mock_is_streaming) {
        return BT_STREAMING_STATE_STOPPED;
    } else if (mock_is_paused) {
        return BT_STREAMING_STATE_PAUSED;
    } else {
        return BT_STREAMING_STATE_PLAYING;
    }
}

// Pairing functions

/**
 * Start mock pairing
 */
esp_err_t bt_mock_start_pairing(const char* addr) {
    if (!addr) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Reset pairing state
    current_pairing_state = BT_PAIRING_STATE_NONE;
    
    // Handle simulated timeout
    if (pairing_timeout_simulated) {
        current_pairing_state = BT_PAIRING_STATE_TIMEOUT;
        pairing_timeout_simulated = false;  // Reset for next test
        return ESP_OK;
    }
    
    // Choose pairing method based on SSP support
    if (ssp_supported && !ssp_confirm_requested) {
        current_pairing_method = BT_PAIRING_SSP;
        current_pairing_state = BT_PAIRING_STATE_SSP_CONFIRM;
    } else {
        current_pairing_method = BT_PAIRING_PIN;
        current_pairing_state = BT_PAIRING_STATE_PIN_REQUESTED;
    }
    
    ESP_LOGI(TAG, "Mock pairing started with %s, method: %d", addr, current_pairing_method);
    return ESP_OK;
}

/**
 * Send PIN for pairing
 */
esp_err_t bt_mock_send_pin_code(const char* pin) {
    if (!pin) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (current_pairing_method != BT_PAIRING_PIN || 
        current_pairing_state != BT_PAIRING_STATE_PIN_REQUESTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check for simulated PIN failure
    if (pin_failure_simulated) {
        current_pairing_state = BT_PAIRING_STATE_FAILED;
        pin_failure_simulated = false;  // Reset for next test
        return ESP_FAIL;
    }
    
    // PIN accepted
    current_pairing_state = BT_PAIRING_STATE_COMPLETE;
    
    // Mark device as paired
    for (int i = 0; i < mock_device_count; i++) {
        char dev_addr[18];
        sprintf(dev_addr, "%02x:%02x:%02x:%02x:%02x:%02x",
                mock_devices[i].addr[0], mock_devices[i].addr[1],
                mock_devices[i].addr[2], mock_devices[i].addr[3],
                mock_devices[i].addr[4], mock_devices[i].addr[5]);
                
        if (strcmp(dev_addr, mock_connected_device) == 0) {
            mock_devices[i].paired = true;
            ESP_LOGI(TAG, "Device paired: %s", dev_addr);
            break;
        }
    }
    
    ESP_LOGI(TAG, "PIN code accepted");
    return ESP_OK;
}

/**
 * Get pairing state
 */
bt_pairing_state_t bt_mock_get_pairing_state(void) {
    return current_pairing_state;
}

/**
 * Get pairing method
 */
bt_pairing_method_t bt_mock_get_pairing_method(void) {
    return current_pairing_method;
}

/**
 * Get SSP passkey
 */
esp_err_t bt_mock_get_ssp_passkey(char* passkey, size_t size) {
    if (!passkey || size < 7) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (current_pairing_method != BT_PAIRING_SSP || 
        current_pairing_state != BT_PAIRING_STATE_SSP_CONFIRM) {
        return ESP_ERR_NOT_FOUND;
    }
    
    strncpy(passkey, ssp_passkey, size - 1);
    passkey[size - 1] = '\0';
    return ESP_OK;
}

/**
 * Check if SSP confirmation is requested
 */
bool bt_mock_is_ssp_confirm_requested(void) {
    return current_pairing_method == BT_PAIRING_SSP && 
           current_pairing_state == BT_PAIRING_STATE_SSP_CONFIRM;
}

/**
 * Respond to SSP confirmation
 */
esp_err_t bt_mock_ssp_confirm(bool confirm) {
    if (current_pairing_method != BT_PAIRING_SSP || 
        current_pairing_state != BT_PAIRING_STATE_SSP_CONFIRM) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (confirm) {
        // SSP confirmed
        current_pairing_state = BT_PAIRING_STATE_COMPLETE;
        
        // Mark device as paired
        for (int i = 0; i < mock_device_count; i++) {
            char dev_addr[18];
            sprintf(dev_addr, "%02x:%02x:%02x:%02x:%02x:%02x",
                    mock_devices[i].addr[0], mock_devices[i].addr[1],
                    mock_devices[i].addr[2], mock_devices[i].addr[3],
                    mock_devices[i].addr[4], mock_devices[i].addr[5]);
                    
            if (strcmp(dev_addr, mock_connected_device) == 0) {
                mock_devices[i].paired = true;
                ESP_LOGI(TAG, "Device paired: %s", dev_addr);
                break;
            }
        }
        
        ESP_LOGI(TAG, "SSP confirmation accepted");
    } else {
        // SSP rejected
        current_pairing_state = BT_PAIRING_STATE_FAILED;
        ESP_LOGI(TAG, "SSP confirmation rejected");
    }
    
    return ESP_OK;
}

/**
 * Set default PIN
 */
esp_err_t bt_mock_set_default_pin(const char* pin) {
    if (!pin || strlen(pin) == 0 || strlen(pin) > 16) {
        return ESP_ERR_INVALID_ARG;
    }
    
    strncpy(default_pin, pin, sizeof(default_pin) - 1);
    default_pin[sizeof(default_pin) - 1] = '\0';
    ESP_LOGI(TAG, "Default PIN set to: %s", default_pin);
    return ESP_OK;
}

/**
 * Get default PIN
 */
esp_err_t bt_mock_get_default_pin(char* pin, size_t size) {
    if (!pin || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (strlen(default_pin) < size) {
        strcpy(pin, default_pin);
    } else {
        strncpy(pin, default_pin, size - 1);
        pin[size - 1] = '\0';
    }
    
    return ESP_OK;
}

/**
 * Unpair a device
 */
esp_err_t bt_mock_unpair_device(const char* addr) {
    if (!addr) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find and unpair device
    for (int i = 0; i < mock_device_count; i++) {
        char dev_addr[18];
        sprintf(dev_addr, "%02x:%02x:%02x:%02x:%02x:%02x",
                mock_devices[i].addr[0], mock_devices[i].addr[1],
                mock_devices[i].addr[2], mock_devices[i].addr[3],
                mock_devices[i].addr[4], mock_devices[i].addr[5]);
                
        if (strcasecmp(dev_addr, addr) == 0) {
            // Found the device
            if (!mock_devices[i].paired) {
                return ESP_ERR_NOT_FOUND;  // Not paired
            }
            
            // If currently connected to this device, disconnect
            if (mock_is_connected && strcasecmp(mock_connected_device, addr) == 0) {
                bt_mock_disconnect();
            }
            
            mock_devices[i].paired = false;
            ESP_LOGI(TAG, "Device unpaired: %s", addr);
            return ESP_OK;
        }
    }
    
    // Device not found
    return ESP_ERR_NOT_FOUND;
}

/**
 * Unpair all devices
 */
esp_err_t bt_mock_unpair_all_devices(void) {
    int unpaired_count = 0;
    
    // Disconnect if connected
    if (mock_is_connected) {
        bt_mock_disconnect();
    }
    
    // Unpair all devices
    for (int i = 0; i < mock_device_count; i++) {
        if (mock_devices[i].paired) {
            mock_devices[i].paired = false;
            unpaired_count++;
        }
    }
    
    ESP_LOGI(TAG, "Unpaired %d devices", unpaired_count);
    return ESP_OK;
}

// Test control functions

/**
 * Simulate PIN failure
 */
void bt_mock_simulate_pin_failure(void) {
    pin_failure_simulated = true;
    ESP_LOGI(TAG, "Simulating PIN failure");
}

/**
 * Simulate pairing timeout
 */
void bt_mock_simulate_pairing_timeout(void) {
    pairing_timeout_simulated = true;
    ESP_LOGI(TAG, "Simulating pairing timeout");
}

/**
 * Simulate SSP request
 */
void bt_mock_simulate_ssp_request(uint32_t passkey) {
    sprintf(ssp_passkey, "%06" PRIu32, passkey); // Fix format string
    ssp_confirm_requested = true;
    current_pairing_method = BT_PAIRING_SSP;
    current_pairing_state = BT_PAIRING_STATE_SSP_CONFIRM;
    ESP_LOGI(TAG, "Simulating SSP request with passkey: %s", ssp_passkey);
}

/**
 * Set SSP support
 */
void bt_mock_set_ssp_supported(bool supported) {
    ssp_supported = supported;
    ESP_LOGI(TAG, "Set SSP support: %d", supported);
}
