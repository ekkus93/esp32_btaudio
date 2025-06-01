#include "bt_source.h"
#include "esp_log.h"
#include <string.h> // Add this include for memcpy

static const char* TAG = "BT_STUB";
static bool s_connected = false;
static bool s_streaming = false;
static uint16_t s_discovered_count = 3; // Simulate finding 3 devices during scan
static uint16_t s_paired_count = 1;     // Simulate 1 paired device by default

// Mock data
static bt_discovery_cb_t s_discovery_callback = NULL;
static void* s_user_data = NULL;
static bt_device_type_t s_scan_filter = BT_DEVICE_TYPE_ANY;

// Mock devices for testing
static bt_device_t s_mock_devices[] = {
    {
        .addr = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
        .name = "Mock A2DP Speaker",
        .rssi = -65,
        .paired = false,
        .connected = false,
        .profiles = BT_PROFILE_A2DP_SINK
    },
    {
        .addr = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},
        .name = "Mock Headphones",
        .rssi = -72,
        .paired = true,
        .connected = false,
        .profiles = BT_PROFILE_A2DP_SINK
    },
    {
        .addr = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC},
        .name = "Mock Car Audio",
        .rssi = -80,
        .paired = false,
        .connected = false,
        .profiles = BT_PROFILE_A2DP_SINK | BT_PROFILE_HFP
    }
};
static const int s_mock_device_count = sizeof(s_mock_devices) / sizeof(s_mock_devices[0]);

// Initialize the Bluetooth stack and A2DP source profile
esp_err_t bt_init(void) {
    ESP_LOGI(TAG, "Stub: Initializing Bluetooth stack");
    return ESP_OK;
}

// Register a discovery callback function
esp_err_t bt_register_discovery_callback(bt_discovery_cb_t callback, void* user_data) {
    s_discovery_callback = callback;
    s_user_data = user_data;
    return ESP_OK;
}

// Start scanning for Bluetooth devices with a filter
esp_err_t bt_scan_start_filtered(bt_device_type_t device_type) {
    ESP_LOGI(TAG, "Stub: Starting BT scan with filter %d", device_type);
    s_scan_filter = device_type;
    return ESP_OK;
}

// Start scanning for Bluetooth devices
esp_err_t bt_scan_start(void) {
    ESP_LOGI(TAG, "Stub: Starting BT scan");
    
    // In our stub, trigger the discovery callback for mock devices
    // This simulates devices being found during scan
    if (s_discovery_callback) {
        for (int i = 0; i < s_mock_device_count; i++) {
            // Apply filter if necessary
            if (s_scan_filter == BT_DEVICE_TYPE_ANY || 
                (s_scan_filter == BT_DEVICE_TYPE_A2DP_SINK && 
                 (s_mock_devices[i].profiles & BT_PROFILE_A2DP_SINK))) {
                s_discovery_callback(&s_mock_devices[i], s_user_data);
            }
        }
    }
    
    return ESP_OK;
}

// Stop scanning for Bluetooth devices
esp_err_t bt_scan_stop(void) {
    ESP_LOGI(TAG, "Stub: Stopping BT scan");
    return ESP_OK;
}

// Get number of discovered devices during scan
uint16_t bt_get_discovered_device_count(void) {
    ESP_LOGI(TAG, "Stub: Getting discovered device count: %d", s_discovered_count);
    return s_discovered_count;
}

// Connect to a Bluetooth device by MAC address
esp_err_t bt_connect(const char* addr) {
    ESP_LOGI(TAG, "Stub: Connecting to device %s", addr);
    s_connected = true;
    return ESP_OK;
}

// Check if connected to a Bluetooth device
bool bt_is_connected(void) {
    return s_connected;
}

// Disconnect from connected Bluetooth device
esp_err_t bt_disconnect(void) {
    ESP_LOGI(TAG, "Stub: Disconnecting from device");
    s_connected = false;
    s_streaming = false;
    return ESP_OK;
}

// Start A2DP audio streaming
esp_err_t bt_start_streaming(void) {
    ESP_LOGI(TAG, "Stub: Starting audio streaming");
    s_streaming = true;
    return ESP_OK;
}

// Stop A2DP audio streaming
esp_err_t bt_stop_streaming(void) {
    ESP_LOGI(TAG, "Stub: Stopping audio streaming");
    s_streaming = false;
    return ESP_OK;
}

// Check if audio is currently streaming
bool bt_is_streaming(void) {
    return s_streaming;
}

// Get the number of paired devices in memory
uint16_t bt_get_paired_device_count(void) {
    return s_paired_count;
}

// Add a device to the paired devices list
esp_err_t bt_add_paired_device(const bt_device_t* device) {
    ESP_LOGI(TAG, "Stub: Adding paired device %02x:%02x:%02x:%02x:%02x:%02x - %s", 
             device->addr[0], device->addr[1], device->addr[2],
             device->addr[3], device->addr[4], device->addr[5],
             device->name);
    s_paired_count++;
    return ESP_OK;
}

// Remove a device from the paired devices list
esp_err_t bt_remove_paired_device(const bt_device_t* device) {
    ESP_LOGI(TAG, "Stub: Removing paired device %02x:%02x:%02x:%02x:%02x:%02x", 
             device->addr[0], device->addr[1], device->addr[2],
             device->addr[3], device->addr[4], device->addr[5]);
    if (s_paired_count > 0) {
        s_paired_count--;
    }
    return ESP_OK;
}

esp_err_t bt_get_discovered_devices(bt_device_t* devices, uint16_t max_count, uint16_t* count) {
    int actual_count = 0;
    
    for (int i = 0; i < s_mock_device_count && actual_count < max_count; i++) {
        // Apply filter if necessary
        if (s_scan_filter == BT_DEVICE_TYPE_ANY || 
            (s_scan_filter == BT_DEVICE_TYPE_A2DP_SINK && 
             (s_mock_devices[i].profiles & BT_PROFILE_A2DP_SINK))) {
            memcpy(&devices[actual_count], &s_mock_devices[i], sizeof(bt_device_t));
            actual_count++;
        }
    }
    
    *count = actual_count;
    return ESP_OK;
}

bool bt_device_supports_profile(const bt_device_t* device, bt_profile_t profile) {
    return device && (device->profiles & profile);
}
