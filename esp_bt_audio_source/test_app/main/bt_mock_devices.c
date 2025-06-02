/**
 * Minimal mock implementation for Bluetooth device testing
 * Only used during tests - not part of production code
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "bt_source.h"
#include "bt_mock_devices.h"

static const char *TAG = "BT_MOCK";

// Constants
#define MAX_MOCK_DEVICES 10

// Static variables for mock device state
static bt_device_t s_mock_devices[MAX_MOCK_DEVICES];
static int s_mock_device_count = 0;
static bool s_mock_scan_active = false;
static bool s_mock_connected = false;
static char s_connected_addr[18];
static bool s_mock_devices_paired[MAX_MOCK_DEVICES] = {false};

// Fix enum values to match bt_source.h
static bt_pairing_state_t s_mock_pairing_state = BT_PAIRING_STATE_IDLE;
static bt_pairing_method_t s_mock_pairing_method = BT_PAIRING_METHOD_NONE;
static char s_mock_default_pin[17] = "1234";
static bool s_mock_ssp_supported = true;
static bool s_mock_ssp_confirm_requested = false;
static uint32_t s_mock_ssp_passkey = 123456;
static char s_mock_current_pairing_addr[18] = {0};
static bool s_mock_is_pairing = false;

// Reset function resets all state variables 
void bt_mock_reset(void) {
    ESP_LOGI(TAG, "Resetting BT mock state");
    s_mock_device_count = 0;
    s_mock_scan_active = false;
    s_mock_connected = false;
    s_mock_pairing_state = BT_PAIRING_STATE_IDLE;
    s_mock_pairing_method = BT_PAIRING_METHOD_NONE;
    s_mock_ssp_confirm_requested = false;
    s_mock_is_pairing = false;
    memset(s_mock_devices, 0, sizeof(s_mock_devices));
    memset(s_mock_devices_paired, 0, sizeof(s_mock_devices_paired));
    memset(s_connected_addr, 0, sizeof(s_connected_addr));
    memset(s_mock_current_pairing_addr, 0, sizeof(s_mock_current_pairing_addr));
}

// Add device with paired state flag
void bt_mock_add_device(const char* addr, const char* name, bt_device_type_t type, bool supports_a2dp) {
    ESP_LOGI(TAG, "Adding mock device: %s (%s)", addr, name);
    
    if (s_mock_device_count >= MAX_MOCK_DEVICES) {
        ESP_LOGW(TAG, "Cannot add device - device list full");
        return;
    }
    
    // Convert address string to byte array
    uint8_t addr_bytes[6];
    if (sscanf(addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
           &addr_bytes[0], &addr_bytes[1], &addr_bytes[2], 
           &addr_bytes[3], &addr_bytes[4], &addr_bytes[5]) != 6) {
        ESP_LOGW(TAG, "Invalid address format");
        return;
    }
    
    // Check if device already exists
    for (int i = 0; i < s_mock_device_count; i++) {
        if (memcmp(s_mock_devices[i].addr, addr_bytes, 6) == 0) {
            // Device already exists - update name and type
            strncpy(s_mock_devices[i].name, name, sizeof(s_mock_devices[i].name) - 1);
            s_mock_devices[i].name[sizeof(s_mock_devices[i].name) - 1] = '\0';
            
            // Set device type based on the type parameter
            if (type == BT_DEVICE_TYPE_AUDIO) {
                s_mock_devices[i].cod = 0x240404; // Audio device
            } else {
                s_mock_devices[i].cod = 0x120104; // Non-audio device
            }
            
            return;
        }
    }
    
    // Add the device to mock devices list
    memcpy(s_mock_devices[s_mock_device_count].addr, addr_bytes, 6);
    strncpy(s_mock_devices[s_mock_device_count].name, name, 
            sizeof(s_mock_devices[s_mock_device_count].name) - 1);
    s_mock_devices[s_mock_device_count].name[sizeof(s_mock_devices[s_mock_device_count].name) - 1] = '\0';
    s_mock_devices[s_mock_device_count].rssi = -70; // Default RSSI value
    
    // Set device type based on the type parameter
    if (type == BT_DEVICE_TYPE_AUDIO) {
        s_mock_devices[s_mock_device_count].cod = 0x240404; // Audio device
    } else {
        s_mock_devices[s_mock_device_count].cod = 0x120104; // Non-audio device
    }
    
    // Mark device as paired by default to match test expectations
    s_mock_devices_paired[s_mock_device_count] = true;
    
    s_mock_device_count++;
    ESP_LOGI(TAG, "Added device %d: %s", s_mock_device_count - 1, name);
}

// Start a mock BT scan
void bt_mock_start_scan(void) {
    ESP_LOGI(TAG, "Starting scan");
    s_mock_scan_active = true;
}

// Stop a mock BT scan
void bt_mock_stop_scan(void) {
    ESP_LOGI(TAG, "Stopping scan");
    s_mock_scan_active = false;
}

// Get scan results
int bt_mock_get_scan_results(bt_device_t* devices, int max_count) {
    // Validate input parameters
    if (devices == NULL || max_count <= 0) {
        return 0;
    }
    
    int count = (max_count < s_mock_device_count) ? max_count : s_mock_device_count;
    
    // Add 3 predefined mock devices for testing if no devices were added
    if (count == 0) {
        // Make sure there's enough space for our 3 mock devices
        if (max_count < 3) {
            // Not enough space, return as many as we can fit
            return 0;
        }
        
        // Add some mock devices with hard-coded values for testing
        uint8_t addr1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
        uint8_t addr2[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        uint8_t addr3[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
        
        // Device 1
        memcpy(devices[0].addr, addr1, 6);
        strncpy(devices[0].name, "Mock Speaker", sizeof(devices[0].name) - 1);
        devices[0].name[sizeof(devices[0].name) - 1] = '\0';
        devices[0].rssi = -65;
        devices[0].cod = 0x240404; // Audio device
        
        // Device 2
        memcpy(devices[1].addr, addr2, 6);
        strncpy(devices[1].name, "Mock Headphones", sizeof(devices[1].name) - 1);
        devices[1].name[sizeof(devices[1].name) - 1] = '\0';
        devices[1].rssi = -72;
        devices[1].cod = 0x240414; // Audio device
        
        // Device 3
        memcpy(devices[2].addr, addr3, 6);
        strncpy(devices[2].name, "Mock Car Kit", sizeof(devices[2].name) - 1);
        devices[2].name[sizeof(devices[2].name) - 1] = '\0';
        devices[2].rssi = -85;
        devices[2].cod = 0x240408; // Audio device
        
        return 3;
    } else {
        // Copy from our mock device list
        if (count > 0) {
            memcpy(devices, s_mock_devices, count * sizeof(bt_device_t));
        }
        return count;
    }
}

// Connect to a mock device
esp_err_t bt_mock_connect(const char* addr) {
    // Validate input parameter
    if (addr == NULL) {
        ESP_LOGE(TAG, "Invalid NULL address parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Connecting to mock device: %s", addr);
    
    if (s_mock_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Convert address string to byte array for comparison
    uint8_t addr_bytes[6];
    if (sscanf(addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
               &addr_bytes[0], &addr_bytes[1], &addr_bytes[2], 
               &addr_bytes[3], &addr_bytes[4], &addr_bytes[5]) != 6) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if device exists and is paired
    bool found = false;
    for (int i = 0; i < s_mock_device_count; i++) {
        if (memcmp(s_mock_devices[i].addr, addr_bytes, 6) == 0) {
            if (s_mock_devices_paired[i]) {
                found = true;
            } else {
                // Device exists but not paired - this matches bt_source.c error
                return ESP_ERR_NOT_FOUND;
            }
            break;
        }
    }
    
    if (!found && s_mock_device_count < MAX_MOCK_DEVICES) {
        // Auto-add device if it doesn't exist yet
        char name[32];
        snprintf(name, sizeof(name), "Mock Device %.8s...", addr);
        bt_mock_add_device(addr, name, BT_DEVICE_TYPE_AUDIO, true);
        
        // Verify the device was actually added
        for (int i = 0; i < s_mock_device_count; i++) {
            if (memcmp(s_mock_devices[i].addr, addr_bytes, 6) == 0) {
                found = true;
                break;
            }
        }
    }
    
    if (found) {
        s_mock_connected = true;
        
        size_t addr_len = strlen(addr);
        if (addr_len >= sizeof(s_connected_addr)) {
            addr_len = sizeof(s_connected_addr) - 1;
        }
        memcpy(s_connected_addr, addr, addr_len);
        s_connected_addr[addr_len] = '\0';
        
        return ESP_OK;
    }
    
    return ESP_ERR_NOT_FOUND;
}

// Disconnect from mock device
esp_err_t bt_mock_disconnect(void) {
    ESP_LOGI(TAG, "Disconnecting from mock device");
    
    if (!s_mock_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_mock_connected = false;
    memset(s_connected_addr, 0, sizeof(s_connected_addr));
    
    return ESP_OK;
}

// Check if connected to mock device
bool bt_mock_is_connected(void) {
    return s_mock_connected;
}

// Get connected device address
const char* bt_mock_get_connected_addr(void) {
    if (!s_mock_connected) {
        return NULL;
    }
    return s_connected_addr;
}

// Start pairing with a mock device
esp_err_t bt_mock_start_pairing(const char* addr) {
    if (addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting pairing with device: %s", addr);
    
    // Store the pairing address
    strncpy(s_mock_current_pairing_addr, addr, sizeof(s_mock_current_pairing_addr) - 1);
    s_mock_current_pairing_addr[sizeof(s_mock_current_pairing_addr) - 1] = '\0';
    s_mock_is_pairing = true;
    
    // Set initial state based on SSP support
    if (s_mock_ssp_supported) {
        s_mock_pairing_method = BT_PAIRING_METHOD_SSP;
        // Don't set SSP_REQUESTED state yet - simulator will do that
    } else {
        s_mock_pairing_method = BT_PAIRING_METHOD_PIN;
        s_mock_pairing_state = BT_PAIRING_STATE_PIN_REQUESTED;
    }
    
    return ESP_OK;
}

// Send PIN code for pairing
esp_err_t bt_mock_send_pin(const char* pin) {
    if (!s_mock_is_pairing || s_mock_pairing_method != BT_PAIRING_METHOD_PIN) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Processing PIN code");
    
    // Check PIN
    if (strcmp(pin, s_mock_default_pin) == 0) {
        s_mock_pairing_state = BT_PAIRING_STATE_PAIRED;
        
        // Add the device to paired list
        uint8_t addr_bytes[6];
        if (sscanf(s_mock_current_pairing_addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &addr_bytes[0], &addr_bytes[1], &addr_bytes[2],
                  &addr_bytes[3], &addr_bytes[4], &addr_bytes[5]) == 6) {
            
            // Find or add device
            int device_idx = -1;
            for (int i = 0; i < s_mock_device_count; i++) {
                if (memcmp(s_mock_devices[i].addr, addr_bytes, 6) == 0) {
                    device_idx = i;
                    break;
                }
            }
            
            if (device_idx == -1 && s_mock_device_count < MAX_MOCK_DEVICES) {
                // Add new device
                memcpy(s_mock_devices[s_mock_device_count].addr, addr_bytes, 6);
                s_mock_devices_paired[s_mock_device_count] = true;
                s_mock_device_count++;
            } else if (device_idx >= 0) {
                s_mock_devices_paired[device_idx] = true;
            }
        }
        
        return ESP_OK;
    } else if (strcmp(pin, "9999") == 0) {
        s_mock_pairing_state = BT_PAIRING_STATE_TIMEOUT;
        return ESP_ERR_TIMEOUT;
    } else {
        s_mock_pairing_state = BT_PAIRING_STATE_FAILED;
        return ESP_FAIL;
    }
}

// Set default PIN code
esp_err_t bt_mock_set_default_pin(const char* pin) {
    if (pin == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t len = strlen(pin);
    if (len == 0 || len >= sizeof(s_mock_default_pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    strncpy(s_mock_default_pin, pin, sizeof(s_mock_default_pin) - 1);
    s_mock_default_pin[sizeof(s_mock_default_pin) - 1] = '\0';
    return ESP_OK;
}

// Get current pairing state
bt_pairing_state_t bt_mock_get_pairing_state(void) {
    return s_mock_pairing_state;
}

// Get current pairing method
bt_pairing_method_t bt_mock_get_pairing_method(void) {
    return s_mock_pairing_method;
}

// Enable or disable SSP support
void bt_mock_set_ssp_supported(bool supported) {
    s_mock_ssp_supported = supported;
}

// Simulate an SSP request with a passkey
void bt_mock_simulate_ssp_request(uint32_t passkey) {
    if (s_mock_ssp_supported) {
        s_mock_pairing_state = BT_PAIRING_STATE_SSP_REQUESTED;
        s_mock_pairing_method = BT_PAIRING_METHOD_SSP;
        s_mock_ssp_confirm_requested = true;
        
        // Use provided passkey if non-zero
        if (passkey > 0) {
            s_mock_ssp_passkey = passkey;
        }
    }
}

// Check if SSP confirmation is requested
bool bt_mock_is_ssp_confirm_requested(void) {
    return s_mock_ssp_confirm_requested;
}

// Get current SSP passkey
uint32_t bt_mock_get_ssp_passkey(void) {
    return s_mock_ssp_passkey;
}

// Confirm or reject SSP pairing
esp_err_t bt_mock_confirm_ssp(bool confirm) {
    if (!s_mock_ssp_confirm_requested) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (confirm) {
        s_mock_pairing_state = BT_PAIRING_STATE_PAIRED;
        
        // Add device to paired list
        uint8_t addr_bytes[6];
        if (sscanf(s_mock_current_pairing_addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &addr_bytes[0], &addr_bytes[1], &addr_bytes[2],
                  &addr_bytes[3], &addr_bytes[4], &addr_bytes[5]) == 6) {
            
            int device_idx = -1;
            for (int i = 0; i < s_mock_device_count; i++) {
                if (memcmp(s_mock_devices[i].addr, addr_bytes, 6) == 0) {
                    device_idx = i;
                    break;
                }
            }
            
            if (device_idx == -1 && s_mock_device_count < MAX_MOCK_DEVICES) {
                memcpy(s_mock_devices[s_mock_device_count].addr, addr_bytes, 6);
                s_mock_devices_paired[s_mock_device_count] = true;
                s_mock_device_count++;
            } else if (device_idx >= 0) {
                s_mock_devices_paired[device_idx] = true;
            }
        }
    } else {
        s_mock_pairing_state = BT_PAIRING_STATE_FAILED;
    }
    
    s_mock_ssp_confirm_requested = false;
    return ESP_OK;
}

// Add a paired device
esp_err_t bt_mock_add_paired_device(const bt_device_t* device) {
    if (device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Look for device in the list
    for (int i = 0; i < s_mock_device_count; i++) {
        if (memcmp(s_mock_devices[i].addr, device->addr, 6) == 0) {
            // Device found, mark as paired
            s_mock_devices_paired[i] = true;
            return ESP_OK;
        }
    }
    
    // Device not found, add it if there's space
    if (s_mock_device_count < MAX_MOCK_DEVICES) {
        memcpy(&s_mock_devices[s_mock_device_count], device, sizeof(bt_device_t));
        s_mock_devices_paired[s_mock_device_count] = true;
        s_mock_device_count++;
        return ESP_OK;
    }
    
    return ESP_ERR_NO_MEM;
}

// Check if a device is paired
bool bt_mock_is_device_paired(const char* addr) {
    if (addr == NULL) {
        return false;
    }
    
    // Convert address to bytes
    uint8_t addr_bytes[6];
    if (sscanf(addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
              &addr_bytes[0], &addr_bytes[1], &addr_bytes[2],
              &addr_bytes[3], &addr_bytes[4], &addr_bytes[5]) != 6) {
        return false;
    }
    
    // Check if device is in paired list
    for (int i = 0; i < s_mock_device_count; i++) {
        if (memcmp(s_mock_devices[i].addr, addr_bytes, 6) == 0) {
            return s_mock_devices_paired[i];
        }
    }
    
    return false;
}

// Unpair a specific device
esp_err_t bt_mock_unpair_device(const char* addr) {
    if (addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if address is all zeros (invalid)
    bool all_zeros = true;
    for (int i = 0; i < strlen(addr); i++) {
        if (addr[i] != '0' && addr[i] != ':') {
            all_zeros = false;
            break;
        }
    }
    
    if (all_zeros) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t addr_bytes[6];
    if (sscanf(addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
              &addr_bytes[0], &addr_bytes[1], &addr_bytes[2],
              &addr_bytes[3], &addr_bytes[4], &addr_bytes[5]) != 6) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < s_mock_device_count; i++) {
        if (memcmp(s_mock_devices[i].addr, addr_bytes, 6) == 0) {
            s_mock_devices_paired[i] = false;
            
            // If connected to this device, disconnect
            if (s_mock_connected && strncmp(s_connected_addr, addr, strlen(addr)) == 0) {
                s_mock_connected = false;
                memset(s_connected_addr, 0, sizeof(s_connected_addr));
            }
            
            return ESP_OK;
        }
    }
    
    // Device not found - return NOT_FOUND which is what bt_source.c would do
    return ESP_ERR_NOT_FOUND;
}

// Unpair all devices
esp_err_t bt_mock_unpair_all_devices(void) {
    for (int i = 0; i < s_mock_device_count; i++) {
        s_mock_devices_paired[i] = false;
    }
    
    // Disconnect if connected
    if (s_mock_connected) {
        s_mock_connected = false;
        memset(s_connected_addr, 0, sizeof(s_connected_addr));
    }
    
    return ESP_OK;
}

// Get paired device count
int bt_mock_get_paired_device_count(void) {
    int count = 0;
    for (int i = 0; i < s_mock_device_count; i++) {
        if (s_mock_devices_paired[i]) {
            count++;
        }
    }
    return count;
}

// Get paired devices
int bt_mock_get_paired_devices(bt_device_t* devices, int max_count) {
    if (devices == NULL || max_count <= 0) {
        return 0;
    }
    
    int count = 0;
    for (int i = 0; i < s_mock_device_count && count < max_count; i++) {
        if (s_mock_devices_paired[i]) {
            memcpy(&devices[count], &s_mock_devices[i], sizeof(bt_device_t));
            count++;
        }
    }
    
    return count;
}

// Safely copy connected address
esp_err_t bt_mock_copy_connected_addr(char* addr_buf, size_t buf_size) {
    if (addr_buf == NULL || buf_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_mock_connected) {
        addr_buf[0] = '\0';
        return ESP_ERR_INVALID_STATE;
    }
    
    strncpy(addr_buf, s_connected_addr, buf_size - 1);
    addr_buf[buf_size - 1] = '\0';
    
    return ESP_OK;
}
