/**
 * Bluetooth Mock Devices Implementation
 */

#include "bt_mock_devices.h"
#include "bt_source.h" // Make sure we have access to bt_device_t
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

#define TAG "BT_MOCK"

// Define our internal state structure - make sure it doesn't conflict with header
typedef struct {
    bt_device_t devices[10];  // Maintain compatibility with bt_device_t 
    int device_count;
    bool connected;
    char connected_addr[18];
    bool scanning;
    bool connect_by_name_hook_enabled;
    char connect_by_name_device[64];
    char connect_by_name_addr[18];
} bt_mock_internal_state_t;  // Renamed to avoid conflict with header

// Global mock state
static bt_mock_internal_state_t mock_state = {0};

void bt_mock_init(void) {
    memset(&mock_state, 0, sizeof(mock_state));
}

void bt_mock_reset(void) {
    memset(&mock_state, 0, sizeof(mock_state));
}

// Add the cleanup function expected in test_app_main.c
void bt_mock_cleanup(void) {
    bt_mock_reset();
}

void bt_mock_add_device(const char* addr, const char* name, bt_device_type_t type, bool paired) {
    if (mock_state.device_count >= 10) {
        ESP_LOGE(TAG, "Cannot add more mock devices, list is full");
        return;
    }
    
    bt_device_t* device = &mock_state.devices[mock_state.device_count];
    
    // Parse the address
    sscanf(addr, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
           &device->addr[0], &device->addr[1], &device->addr[2],
           &device->addr[3], &device->addr[4], &device->addr[5]);
    
    // Copy the name
    strncpy(device->name, name, sizeof(device->name) - 1);
    device->name[sizeof(device->name) - 1] = '\0';
    
    // Store paired state
    device->paired = paired;
    
    // We can't set the type directly because bt_device_t doesn't have that field
    // Instead we can use the cod field if needed for device type
    switch (type) {
        case BT_DEVICE_TYPE_AUDIO:
            device->cod = 0x240404; // Audio device
            break;
        case BT_DEVICE_TYPE_PHONE:
            device->cod = 0x500204; // Phone device
            break;
        default:
            device->cod = 0x120104; // Generic device
            break;
    }
    
    mock_state.device_count++;
}

esp_err_t bt_mock_connect(const char* addr) {
    if (mock_state.connected) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Save the address
    strncpy(mock_state.connected_addr, addr, sizeof(mock_state.connected_addr) - 1);
    mock_state.connected_addr[sizeof(mock_state.connected_addr) - 1] = '\0';
    
    mock_state.connected = true;
    return ESP_OK;
}

esp_err_t bt_mock_disconnect(void) {
    if (!mock_state.connected) {
        return ESP_ERR_INVALID_STATE;
    }
    
    mock_state.connected = false;
    memset(mock_state.connected_addr, 0, sizeof(mock_state.connected_addr));
    return ESP_OK;
}

bool bt_mock_is_connected(void) {
    return mock_state.connected;
}

void bt_mock_start_scan(void) {
    mock_state.scanning = true;
}

void bt_mock_stop_scan(void) {
    mock_state.scanning = false;
}

int bt_mock_get_scan_results(bt_device_t* devices, int max_count) {
    if (!devices || max_count <= 0) {
        return 0;
    }
    
    int count = (mock_state.device_count <= max_count) ? 
                 mock_state.device_count : max_count;
    
    memcpy(devices, mock_state.devices, count * sizeof(bt_device_t));
    return count;
}

/**
 * Implements the hook for connect_by_name to update mock connection state
 */
void bt_mock_set_connect_by_name_hook(const char* name, const char* addr) {
    if (name == NULL || addr == NULL) {
        mock_state.connect_by_name_hook_enabled = false;
        return;
    }
    
    mock_state.connect_by_name_hook_enabled = true;
    
    // Copy name with bounds checking
    strncpy(mock_state.connect_by_name_device, name, sizeof(mock_state.connect_by_name_device) - 1);
    mock_state.connect_by_name_device[sizeof(mock_state.connect_by_name_device) - 1] = '\0';
    
    // Copy address with bounds checking
    strncpy(mock_state.connect_by_name_addr, addr, sizeof(mock_state.connect_by_name_addr) - 1);
    mock_state.connect_by_name_addr[sizeof(mock_state.connect_by_name_addr) - 1] = '\0';
}

bool bt_mock_is_device_paired(const char* addr) {
    for (int i = 0; i < mock_state.device_count; i++) {
        char device_addr[18];
        sprintf(device_addr, "%02x:%02x:%02x:%02x:%02x:%02x",
                mock_state.devices[i].addr[0], mock_state.devices[i].addr[1], 
                mock_state.devices[i].addr[2], mock_state.devices[i].addr[3],
                mock_state.devices[i].addr[4], mock_state.devices[i].addr[5]);
        
        if (strcmp(device_addr, addr) == 0) {
            return mock_state.devices[i].paired;
        }
    }
    return false;
}

esp_err_t bt_mock_add_paired_device(const bt_device_t* device) {
    if (!device || mock_state.device_count >= 10) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(&mock_state.devices[mock_state.device_count], device, sizeof(bt_device_t));
    mock_state.devices[mock_state.device_count].paired = true;
    mock_state.device_count++;
    
    return ESP_OK;
}

// This is a mock-specific implementation that shouldn't be exported
static esp_err_t bt_mock_connect_by_name(const char* device_name) {
    ESP_LOGI("BT_MOCK", "Mock: bt_connect_by_name to %s", device_name);
    
    // If the hook is enabled and the name matches, update connection state
    if (mock_state.connect_by_name_hook_enabled && 
        strcmp(device_name, mock_state.connect_by_name_device) == 0) {
        
        ESP_LOGI("BT_MOCK", "Connect by name hook triggered, connecting to %s", 
                mock_state.connect_by_name_addr);
                
        // Connect using the stored address
        return bt_mock_connect(mock_state.connect_by_name_addr);
    }
    
    // If no hook or name doesn't match, return OK but don't connect
    return ESP_OK;
}

// Keep the exported function that properly forwards to the real implementation
esp_err_t bt_mock_hook_connect_by_name(const char* name) {
    return bt_mock_connect_by_name(name);
}
