/**
 * @file bt_interface.c
 * @brief Bluetooth interface implementation
 */

#include <string.h>
#include "esp_log.h"
#include "bt_interface.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "BT_INTERFACE";

// Mock paired devices for testing
static bt_device_info_t paired_devices[5] = {
    {{0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, "Test Device 1", true},
    {{0x22, 0x33, 0x44, 0x55, 0x66, 0x77}, "Test Device 2", true},
    {{0x33, 0x44, 0x55, 0x66, 0x77, 0x88}, "Test Device 3", true},
    {{0x44, 0x55, 0x66, 0x77, 0x88, 0x99}, "Test Speaker", true},
    {{0x55, 0x66, 0x77, 0x88, 0x99, 0xAA}, "Test Headphones", true}
};

static int paired_device_count = 5;
static bool is_connected = false;
static bool is_streaming = false;
static bool is_paused = false;
static bt_device_info_t connected_device = {{0}, "Not Connected", false};
static bt_stream_state_t stream_state = BT_STREAM_STATE_IDLE;
static bt_pairing_state_t pairing_state = BT_PAIRING_STATE_IDLE;
static bt_pairing_method_t pairing_method = BT_PAIRING_METHOD_NONE;
static char default_pin[17] = "1234";
static uint32_t ssp_passkey = 123456;
static bool ssp_confirm_requested = false;
static bool is_scanning = false;
static int discovered_device_count = 0;
static bool auto_reconnect_enabled = false;

// Add these variables to track paired device states
static bool paired_devices_stored = false;
static int stored_device_count = 0;
static bt_device_info_t stored_devices[10]; // For simulating persistent storage

esp_err_t bt_init(void) {
    ESP_LOGI(TAG, "Initializing Bluetooth");
    
    // In a real implementation, this would initialize the ESP32 Bluetooth stack
    
    return ESP_OK;
}

esp_err_t bt_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing Bluetooth");
    
    // In a real implementation, this would deinitialize the ESP32 Bluetooth stack
    
    return ESP_OK;
}

esp_err_t bt_connect(esp_bd_addr_t addr) {
    ESP_LOGI(TAG, "Connecting to device");
    
    // Find device in paired devices
    for (int i = 0; i < paired_device_count; i++) {
        if (memcmp(addr, paired_devices[i].addr, ESP_BD_ADDR_LEN) == 0) {
            memcpy(&connected_device, &paired_devices[i], sizeof(bt_device_info_t));
            is_connected = true;
            ESP_LOGI(TAG, "Connected to %s", connected_device.name);
            return ESP_OK;
        }
    }
    
    ESP_LOGW(TAG, "Device not found in paired devices");
    return ESP_ERR_NOT_FOUND;
}

esp_err_t bt_disconnect(void) {
    ESP_LOGI(TAG, "Disconnecting from device");
    
    if (!is_connected) {
        ESP_LOGW(TAG, "Not connected to any device");
        return ESP_ERR_INVALID_STATE;
    }
    
    is_connected = false;
    is_streaming = false;
    is_paused = false;
    stream_state = BT_STREAM_STATE_IDLE;
    
    ESP_LOGI(TAG, "Disconnected from %s", connected_device.name);
    strcpy(connected_device.name, "Not Connected");
    
    return ESP_OK;
}

bool bt_is_connected(void) {
    return is_connected;
}

esp_err_t bt_get_connection_info(bt_device_info_t *info) {
    if (!is_connected) {
        ESP_LOGW(TAG, "Not connected to any device");
        return ESP_ERR_INVALID_STATE;
    }
    
    memcpy(info, &connected_device, sizeof(bt_device_info_t));
    
    return ESP_OK;
}

esp_err_t bt_scan(void) {
    ESP_LOGI(TAG, "Starting device scan");
    
    // In a real implementation, this would start a Bluetooth scan
    is_scanning = true; // Set scanning state
    
    // Schedule a timeout (for test_bluetooth_scan_timeout)
    // For the mock, we'll simulate this by immediately resetting for now
    // In real implementation, you'd schedule a timeout
    
    return ESP_OK;
}

esp_err_t bt_scan_stop(void) {
    ESP_LOGI(TAG, "Stopping device scan");
    
    // In a real implementation, this would stop a Bluetooth scan
    is_scanning = false; // Make sure we reset the scanning flag
    
    return ESP_OK;
}

esp_err_t bt_scan_start(void) {
    ESP_LOGI(TAG, "Starting Bluetooth scan");
    is_scanning = true;
    
    // In a real implementation, this would start a Bluetooth scan
    // For the test mock, we'll just populate our discovered devices
    discovered_device_count = paired_device_count;
    
    return ESP_OK;
}

esp_err_t bt_scan_start_filtered(bt_device_type_t device_type) {
    ESP_LOGI(TAG, "Starting Bluetooth scan with filter: %d", device_type);
    is_scanning = true;
    
    // In a real implementation, this would start a filtered Bluetooth scan
    // For our mock, we'll filter based on our test data
    if (device_type == BT_DEVICE_TYPE_AUDIO) {
        // Only include A2DP devices
        discovered_device_count = 0;
        for (int i = 0; i < paired_device_count; i++) {
            if (paired_devices[i].supports_a2dp) {
                discovered_device_count++;
            }
        }
    } else {
        // No filter applied
        discovered_device_count = paired_device_count;
    }
    
    return ESP_OK;
}

bool bt_is_scanning(void) {
    return is_scanning;
}

int bt_get_discovered_device_count(void) {
    return discovered_device_count;
}

int bt_get_discovered_devices(bt_device_info_t *devices, int count, uint16_t *actual_count) {
    // Return a fixed list of devices for testing
    int num_to_copy = count < paired_device_count ? count : paired_device_count;
    memcpy(devices, paired_devices, num_to_copy * sizeof(bt_device_info_t));
    
    // Set the actual count
    if (actual_count != NULL) {
        *actual_count = num_to_copy;
    }
    
    return ESP_OK;
}

esp_err_t bt_connect_by_name(const char *name) {
    ESP_LOGI(TAG, "Connecting to device by name: %s", name);
    
    // Find device in paired devices
    for (int i = 0; i < paired_device_count; i++) {
        if (strcmp(name, paired_devices[i].name) == 0) {
            memcpy(&connected_device, &paired_devices[i], sizeof(bt_device_info_t));
            is_connected = true;
            ESP_LOGI(TAG, "Connected to %s", connected_device.name);
            return ESP_OK;
        }
    }
    
    ESP_LOGW(TAG, "Device not found in paired devices");
    return ESP_ERR_NOT_FOUND;
}

esp_err_t bt_connect_with_timeout(esp_bd_addr_t addr, int timeout_ms) {
    ESP_LOGI(TAG, "Connecting to device with timeout: %d ms", timeout_ms);
    
    // For testing we'll simulate connection failure for certain addresses
    // Consider the last byte - if it's 0xFF, simulate failure
    if (addr[5] == 0xFF) {
        ESP_LOGW(TAG, "Connection timeout (simulated)");
        return ESP_ERR_TIMEOUT;
    }
    
    // Otherwise, proceed with normal connection
    return bt_connect(addr);
}

esp_err_t bt_start_streaming(void) {
    ESP_LOGI(TAG, "Starting audio streaming");
    
    if (!is_connected) {
        ESP_LOGW(TAG, "Not connected to any device");
        return ESP_ERR_INVALID_STATE;
    }
    
    is_streaming = true;
    is_paused = false;
    stream_state = BT_STREAM_STATE_STREAMING;
    
    return ESP_OK;
}

esp_err_t bt_stop_streaming(void) {
    ESP_LOGI(TAG, "Stopping audio streaming");
    
    if (!is_streaming) {
        ESP_LOGW(TAG, "Not streaming");
        return ESP_ERR_INVALID_STATE;
    }
    
    is_streaming = false;
    is_paused = false;
    stream_state = BT_STREAM_STATE_IDLE;
    
    return ESP_OK;
}

esp_err_t bt_pause_streaming(void) {
    ESP_LOGI(TAG, "Pausing audio streaming");
    
    if (!is_streaming) {
        ESP_LOGW(TAG, "Not streaming");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (is_paused) {
        ESP_LOGW(TAG, "Already paused");
        return ESP_ERR_INVALID_STATE;
    }
    
    is_paused = true;
    stream_state = BT_STREAM_STATE_PAUSED;
    
    return ESP_OK;
}

esp_err_t bt_resume_streaming(void) {
    ESP_LOGI(TAG, "Resuming audio streaming");
    
    if (!is_streaming) {
        ESP_LOGW(TAG, "Not streaming");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!is_paused) {
        ESP_LOGW(TAG, "Not paused");
        return ESP_ERR_INVALID_STATE;
    }
    
    is_paused = false;
    stream_state = BT_STREAM_STATE_STREAMING;
    
    return ESP_OK;
}

bool bt_is_streaming(void) {
    return is_streaming;
}

bool bt_is_paused(void) {
    return is_paused;
}

bt_stream_state_t bt_get_streaming_state(void) {
    return stream_state;
}

bool bt_device_supports_profile(esp_bd_addr_t addr, uint16_t profile_id) {
    // For testing, assume all paired devices support A2DP
    for (int i = 0; i < paired_device_count; i++) {
        if (memcmp(addr, paired_devices[i].addr, ESP_BD_ADDR_LEN) == 0) {
            return paired_devices[i].supports_a2dp;
        }
    }
    
    return false;
}

esp_err_t bt_start_pairing(esp_bd_addr_t addr) {
    ESP_LOGI(TAG, "Starting pairing with device");
    
    // Set initial pairing state
    pairing_state = BT_PAIRING_STATE_STARTED;
    
    // Simulate different pairing methods based on the device address
    if (addr[0] == 0x11) {
        // Simulate PIN pairing request
        pairing_state = BT_PAIRING_STATE_PIN_REQUESTED;
        pairing_method = BT_PAIRING_METHOD_PIN;
    } else if (addr[0] == 0x22) {
        // Simulate SSP pairing request
        pairing_state = BT_PAIRING_STATE_SSP_REQUESTED;
        pairing_method = BT_PAIRING_METHOD_SSP;
        ssp_confirm_requested = true;
    } else {
        // Default to PIN for other addresses
        pairing_state = BT_PAIRING_STATE_PIN_REQUESTED;
        pairing_method = BT_PAIRING_METHOD_PIN;
    }
    
    return ESP_OK;
}

bool bt_is_device_paired(esp_bd_addr_t addr) {
    // Check if the device is in our paired list
    for (int i = 0; i < paired_device_count; i++) {
        if (memcmp(addr, paired_devices[i].addr, ESP_BD_ADDR_LEN) == 0) {
            return true;
        }
    }
    
    return false;
}

bt_pairing_state_t bt_get_pairing_state(void) {
    return pairing_state;
}

bt_pairing_method_t bt_get_pairing_method(void) {
    return pairing_method;
}

esp_err_t bt_send_pin_code(const char *pin) {
    ESP_LOGI(TAG, "Sending PIN code");
    
    // For testing, simulate different states based on conditions
    if (pairing_state != BT_PAIRING_STATE_PIN_REQUESTED) {
        ESP_LOGW(TAG, "PIN not requested");
        return ESP_ERR_INVALID_STATE;
    }
    
    // For testing PIN failures
    if (strcmp(pin, "0000") == 0) {
        pairing_state = BT_PAIRING_STATE_FAILED;
        return ESP_ERR_INVALID_ARG; // Wrong PIN
    }
    
    // For testing timeouts
    if (strcmp(pin, "9999") == 0) {
        pairing_state = BT_PAIRING_STATE_TIMEOUT;
        return ESP_ERR_TIMEOUT;
    }
    
    // For successful pairing
    pairing_state = BT_PAIRING_STATE_PAIRED;
    return ESP_OK;
}

esp_err_t bt_set_default_pin(const char *pin) {
    ESP_LOGI(TAG, "Setting default PIN code");
    
    if (strlen(pin) > 16) {
        ESP_LOGW(TAG, "PIN too long");
        return ESP_ERR_INVALID_ARG;
    }
    
    strcpy(default_pin, pin);
    
    return ESP_OK;
}

esp_err_t bt_get_default_pin(char *pin, size_t size) {
    if (size < strlen(default_pin) + 1) {
        ESP_LOGW(TAG, "Buffer too small");
        return ESP_ERR_INVALID_ARG;
    }
    
    strcpy(pin, default_pin);
    
    return ESP_OK;
}

uint32_t bt_get_ssp_passkey(void) {
    return ssp_passkey;
}

bool bt_is_ssp_confirm_requested(void) {
    return ssp_confirm_requested;
}

esp_err_t bt_ssp_confirm(bool confirm) {
    ESP_LOGI(TAG, "SSP confirmation: %d", confirm);
    
    // For test_ssp_confirmation_accepted and test_ssp_confirmation_rejected
    if (pairing_state != BT_PAIRING_STATE_SSP_REQUESTED) {
        ESP_LOGW(TAG, "SSP confirmation not requested");
        return ESP_ERR_INVALID_STATE;
    }
    
    ssp_confirm_requested = false;
    
    if (confirm) {
        pairing_state = BT_PAIRING_STATE_PAIRED;
    } else {
        pairing_state = BT_PAIRING_STATE_FAILED;
    }
    
    return ESP_OK;
}

esp_err_t bt_unpair_device(esp_bd_addr_t addr) {
    ESP_LOGI(TAG, "Unpairing device");
    
    // Handle invalid address
    if (addr[0] == 0 && addr[1] == 0 && addr[2] == 0 && 
        addr[3] == 0 && addr[4] == 0 && addr[5] == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find the device in the paired list
    for (int i = 0; i < paired_device_count; i++) {
        if (memcmp(addr, paired_devices[i].addr, ESP_BD_ADDR_LEN) == 0) {
            // Remove device from paired devices
            for (int j = i; j < paired_device_count - 1; j++) {
                memcpy(&paired_devices[j], &paired_devices[j + 1], sizeof(bt_device_info_t));
            }
            paired_device_count--;
            
            // If the unpaired device was connected, disconnect it
            if (is_connected && memcmp(addr, connected_device.addr, ESP_BD_ADDR_LEN) == 0) {
                bt_disconnect();
            }
            
            return ESP_OK;
        }
    }
    
    ESP_LOGW(TAG, "Device not found in paired devices");
    return ESP_ERR_NOT_FOUND;
}

esp_err_t bt_unpair_all_devices(void) {
    ESP_LOGI(TAG, "Unpairing all devices");
    
    paired_device_count = 0;
    
    // If connected, disconnect
    if (is_connected) {
        bt_disconnect();
    }
    
    return ESP_OK;
}

int bt_get_paired_device_count(void) {
    return paired_device_count;
}

int bt_get_paired_devices(bt_device_info_t *devices, int count) {
    int num_to_copy = count < paired_device_count ? count : paired_device_count;
    memcpy(devices, paired_devices, num_to_copy * sizeof(bt_device_info_t));
    
    return num_to_copy;
}

esp_err_t bt_get_paired_device_info(esp_bd_addr_t addr, bt_device_info_t *info) {
    for (int i = 0; i < paired_device_count; i++) {
        if (memcmp(addr, paired_devices[i].addr, ESP_BD_ADDR_LEN) == 0) {
            memcpy(info, &paired_devices[i], sizeof(bt_device_info_t));
            return ESP_OK;
        }
    }
    
    ESP_LOGW(TAG, "Device not found in paired devices");
    return ESP_ERR_NOT_FOUND;
}

esp_err_t bt_store_paired_devices(void) {
    ESP_LOGI(TAG, "Storing paired devices");
    
    // Copy current paired devices to the stored list
    memcpy(stored_devices, paired_devices, sizeof(bt_device_info_t) * paired_device_count);
    stored_device_count = paired_device_count;
    paired_devices_stored = true;
    
    return ESP_OK;
}

esp_err_t bt_load_paired_devices(void) {
    ESP_LOGI(TAG, "Loading paired devices");
    
    // If we have stored devices, restore them
    if (paired_devices_stored) {
        memcpy(paired_devices, stored_devices, sizeof(bt_device_info_t) * stored_device_count);
        paired_device_count = stored_device_count;
    }
    
    return ESP_OK;
}

esp_err_t bt_set_auto_reconnect(bool enable) {
    ESP_LOGI(TAG, "Auto reconnect %s", enable ? "enabled" : "disabled");
    auto_reconnect_enabled = enable;
    return ESP_OK;
}

esp_err_t bt_simulate_disconnect(void) {
    ESP_LOGI(TAG, "Simulating device disconnect");
    
    if (!is_connected) {
        ESP_LOGW(TAG, "Not connected to any device");
        return ESP_ERR_INVALID_STATE;
    }
    
    bool was_connected = is_connected;
    bt_device_info_t prev_device;
    memcpy(&prev_device, &connected_device, sizeof(bt_device_info_t));
    
    // Simulate disconnect
    is_connected = false;
    is_streaming = false;
    is_paused = false;
    stream_state = BT_STREAM_STATE_IDLE;
    
    // If auto reconnect is enabled, reconnect
    if (auto_reconnect_enabled && was_connected) {
        ESP_LOGI(TAG, "Auto reconnecting to %s", prev_device.name);
        memcpy(&connected_device, &prev_device, sizeof(bt_device_info_t));
        is_connected = true;
    }
    
    return ESP_OK;
}

// Utility function to add a device to the paired list
esp_err_t bt_add_paired_device(esp_bd_addr_t addr, const char *name, bool supports_a2dp) {
    // Check if we have room for more devices
    if (paired_device_count >= 10) {
        return ESP_ERR_NO_MEM;
    }
    
    // Add the device to our paired list
    memcpy(paired_devices[paired_device_count].addr, addr, ESP_BD_ADDR_LEN);
    strncpy(paired_devices[paired_device_count].name, name, BT_MAX_DEVICE_NAME_LEN);
    paired_devices[paired_device_count].name[BT_MAX_DEVICE_NAME_LEN - 1] = '\0'; // Ensure null termination
    paired_devices[paired_device_count].supports_a2dp = supports_a2dp;
    
    paired_device_count++;
    
    return ESP_OK;
}

// Reset paired devices (for test setup)
esp_err_t bt_reset_paired_devices(void) {
    paired_device_count = 0;
    return ESP_OK;
}

int bt_interface_get_scanned_devices(bt_device_info_t* devices, int max_count) {
    if (devices == NULL || max_count <= 0) {
        return 0;
    }
    
    // Get actual scanned devices - just use our paired devices as mock data
    int actual_count = max_count < discovered_device_count ? max_count : discovered_device_count;
    
    // Copy the data to the caller's buffer
    memcpy(devices, paired_devices, actual_count * sizeof(bt_device_info_t));
    
    // Return the number of devices copied
    return actual_count;
}
