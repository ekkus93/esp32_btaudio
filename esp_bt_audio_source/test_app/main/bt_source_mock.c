#include "bt_source.h"
#include "esp_log.h"
#include <string.h> // Add missing include for memcpy

static const char* TAG = "BT_MOCK";

// Mock device data
static bt_device_t discovered_devices[5] = {
    {
        .addr = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55},
        .name = "BT Speaker",
        .rssi = -70,
        .paired = false
    },
    {
        .addr = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},
        .name = "BT Headphones",
        .rssi = -65,
        .paired = true
    },
    {
        .addr = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
        .name = "Car Audio",
        .rssi = -80,
        .paired = false
    }
};

static uint16_t discovered_count = 3;
static bool is_connected = false;
static bool is_streaming = false;
static uint16_t paired_count = 1;
static bt_discovery_cb_t discovery_callback = NULL;
static void* discovery_user_data = NULL;

// Initialize Bluetooth stack
esp_err_t bt_init(void) {
    ESP_LOGI(TAG, "Mock BT initialized");
    return ESP_OK;
}

// Start scanning
esp_err_t bt_scan_start(void) {
    ESP_LOGI(TAG, "Mock BT scan started");
    
    // Call discovery callback if registered
    if (discovery_callback) {
        discovery_callback(&discovered_devices[0], discovery_user_data);
    }
    
    return ESP_OK;
}

// Start filtered scan
esp_err_t bt_scan_start_filtered(bt_device_type_t device_type) {
    ESP_LOGI(TAG, "Mock BT filtered scan started for type %d", device_type);
    
    // Call discovery callback if registered
    if (discovery_callback) {
        discovery_callback(&discovered_devices[0], discovery_user_data);
    }
    
    return ESP_OK;
}

// Stop scanning
esp_err_t bt_scan_stop(void) {
    ESP_LOGI(TAG, "Mock BT scan stopped");
    return ESP_OK;
}

// Get discovered device count
uint16_t bt_get_discovered_device_count(void) {
    return discovered_count;
}

// Connect to device
esp_err_t bt_connect(const char* addr) {
    ESP_LOGI(TAG, "Mock connecting to %s", addr);
    is_connected = true;
    return ESP_OK;
}

// Check connection status
bool bt_is_connected(void) {
    return is_connected;
}

// Disconnect
esp_err_t bt_disconnect(void) {
    ESP_LOGI(TAG, "Mock disconnecting");
    is_connected = false;
    return ESP_OK;
}

// Start streaming
esp_err_t bt_start_streaming(void) {
    ESP_LOGI(TAG, "Mock streaming started");
    is_streaming = true;
    return ESP_OK;
}

// Stop streaming
esp_err_t bt_stop_streaming(void) {
    ESP_LOGI(TAG, "Mock streaming stopped");
    is_streaming = false;
    return ESP_OK;
}

// Check streaming status
bool bt_is_streaming(void) {
    return is_streaming;
}

// Get paired device count
uint16_t bt_get_paired_device_count(void) {
    return paired_count;
}

// Add paired device - Updated to match header (removed const)
esp_err_t bt_add_paired_device(bt_device_t* device) {
    ESP_LOGI(TAG, "Mock adding paired device: %s", device->name);
    paired_count++;
    return ESP_OK;
}

// Remove paired device - Updated to match header (removed const)
esp_err_t bt_remove_paired_device(bt_device_t* device) {
    ESP_LOGI(TAG, "Mock removing paired device: %s", device->name);
    if (paired_count > 0) {
        paired_count--;
    }
    return ESP_OK;
}

// Register discovery callback
esp_err_t bt_register_discovery_callback(bt_discovery_cb_t callback, void* user_data) {
    discovery_callback = callback;
    discovery_user_data = user_data;
    return ESP_OK;
}

// Get discovered devices - Updated parameter to match header (int instead of uint16_t)
esp_err_t bt_get_discovered_devices(bt_device_t* devices, int max_count, uint16_t* count) {
    uint16_t copy_count = discovered_count;
    if (copy_count > max_count) {
        copy_count = max_count;
    }
    
    for (int i = 0; i < copy_count; i++) {
        memcpy(&devices[i], &discovered_devices[i], sizeof(bt_device_t));
    }
    
    *count = copy_count;
    return ESP_OK;
}

// Check if device supports profile
bool bt_device_supports_profile(const bt_device_t* device, bt_profile_t profile) {
    // In our mock implementation, we'll say all devices support A2DP sink profile
    return (profile == BT_PROFILE_A2DP_SINK);
}
