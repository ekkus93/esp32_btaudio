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

// Add these new variables to support pairing functionality
static char s_mock_default_pin[17] = "1234";
static bt_pairing_state_t s_mock_pairing_state = BT_PAIRING_STATE_IDLE;
static bt_pairing_method_t s_mock_pairing_method = BT_PAIRING_METHOD_NONE;
static bool s_mock_ssp_supported = true;
static bool s_mock_ssp_confirm_requested = false;
static uint32_t s_mock_ssp_passkey = 123456;

// Initialize mock system
void bt_mock_reset(void) {
    ESP_LOGI(TAG, "Resetting BT mock state");
    s_mock_device_count = 0;
    s_mock_scan_active = false;
    s_mock_connected = false;
    memset(s_mock_devices, 0, sizeof(s_mock_devices));
    memset(s_connected_addr, 0, sizeof(s_connected_addr));
}

// Add a mock device for testing
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
    
    s_mock_device_count++;
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
        
        // Device 1 - Use safer string functions
        memcpy(devices[0].addr, addr1, 6);
        strncpy(devices[0].name, "Mock Speaker", sizeof(devices[0].name) - 1);
        devices[0].name[sizeof(devices[0].name) - 1] = '\0';  // Ensure null termination
        devices[0].rssi = -65;
        devices[0].cod = 0x240404; // Audio device
        
        // Device 2 - Use safer string functions
        memcpy(devices[1].addr, addr2, 6);
        strncpy(devices[1].name, "Mock Headphones", sizeof(devices[1].name) - 1);
        devices[1].name[sizeof(devices[1].name) - 1] = '\0';  // Ensure null termination
        devices[1].rssi = -72;
        devices[1].cod = 0x240414; // Audio device
        
        // Device 3 - Use safer string functions
        memcpy(devices[2].addr, addr3, 6);
        strncpy(devices[2].name, "Mock Car Kit", sizeof(devices[2].name) - 1);
        devices[2].name[sizeof(devices[2].name) - 1] = '\0';  // Ensure null termination
        devices[2].rssi = -85;
        devices[2].cod = 0x240408; // Audio device
        
        return 3;
    } else {
        // Copy from our mock device list with proper size validation
        if (count > 0) {
            memcpy(devices, s_mock_devices, count * sizeof(bt_device_t));
        }
        return count;
    }
}

// Connect to a mock device - Add validation and safer string handling
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
    
    // Check if device exists in our mock list
    bool found = false;
    for (int i = 0; i < s_mock_device_count; i++) {
        if (memcmp(s_mock_devices[i].addr, addr_bytes, 6) == 0) {
            found = true;
            break;
        }
    }
    
    if (!found && s_mock_device_count < MAX_MOCK_DEVICES) {
        // Add a new mock device if address not found - Use safer string handling
        char name[32];
        // Use snprintf to prevent buffer overflow
        int result = snprintf(name, sizeof(name), "Mock Device %.8s...", addr);
        if (result < 0 || (size_t)result >= sizeof(name)) {
            // Handle truncation or error
            name[sizeof(name) - 1] = '\0';
        }
        
        // Don't set found=true until we confirm add_device worked
        bt_mock_add_device(addr, name, BT_DEVICE_TYPE_AUDIO, true);
        
        // Verify the device was actually added before setting found=true
        for (int i = 0; i < s_mock_device_count; i++) {
            if (memcmp(s_mock_devices[i].addr, addr_bytes, 6) == 0) {
                found = true;
                break;
            }
        }
    }
    
    if (found) {
        s_mock_connected = true;
        
        // Use safer string handling with explicit size checks
        size_t addr_len = strlen(addr);
        if (addr_len >= sizeof(s_connected_addr)) {
            // Truncate if address is too long
            addr_len = sizeof(s_connected_addr) - 1;
        }
        memcpy(s_connected_addr, addr, addr_len);
        s_connected_addr[addr_len] = '\0';  // Ensure null termination
        
        return ESP_OK;
    }
    
    return ESP_FAIL;
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

// Helper function to convert byte address to string - Add safety checks
static void addr_bytes_to_str(const uint8_t* addr_bytes, char* addr_str) {
    // Validate input parameters
    if (addr_bytes == NULL || addr_str == NULL) {
        return;
    }
    
    // Use snprintf instead of sprintf to prevent buffer overflow
    snprintf(addr_str, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
            addr_bytes[0], addr_bytes[1], addr_bytes[2],
            addr_bytes[3], addr_bytes[4], addr_bytes[5]);
}

// Add a new function to safely get connected address info
esp_err_t bt_mock_copy_connected_addr(char* addr_buf, size_t buf_size) {
    if (addr_buf == NULL || buf_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_mock_connected) {
        addr_buf[0] = '\0';
        return ESP_ERR_INVALID_STATE;
    }
    
    // Safely copy the connected address to caller's buffer
    strncpy(addr_buf, s_connected_addr, buf_size - 1);
    addr_buf[buf_size - 1] = '\0';  // Ensure null termination
    
    return ESP_OK;
}

// Set the default PIN code for pairing
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

// Get the current pairing state
bt_pairing_state_t bt_mock_get_pairing_state(void) {
    return s_mock_pairing_state;
}

// Get the current pairing method
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

// Check if an SSP confirmation is requested
bool bt_mock_is_ssp_confirm_requested(void) {
    return s_mock_ssp_confirm_requested;
}

// Get the current SSP passkey
uint32_t bt_mock_get_ssp_passkey(void) {
    return s_mock_ssp_passkey;
}
