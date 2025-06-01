#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h> 
#include "esp_log.h"
#include "esp_err.h"
#include "bt_source.h"

static const char* TAG = "BT_SOURCE_MOCK";

/* Static variables to track connection and device state */
static bool s_initialized = false;
static bool s_connected = false;
static bool s_streaming = false;
static char s_connected_addr[18] = {0};
static char s_connected_name[32] = "Mock Device";
static uint16_t s_paired_count = 1;
static bool s_scanning = false;
static uint16_t s_discovered_count = 0;
static bt_device_t s_discovered_devices[10];
static bt_connection_info_t s_current_connection = {0};

/* Connection callback function pointer */
static bt_connection_callback_t s_connection_callback = NULL;
static void* s_connection_callback_data = NULL;

/* Streaming callback function pointer */
static bt_stream_callback_t s_stream_callback = NULL;
static void* s_stream_callback_data = NULL;

/* Magic bytes array for paired devices test */
static uint8_t s_paired_devices[] = {
    0x1B, 0xFB, 0x3F, 0xA0, 0x1B, 0xFB, 0x3F, 0x0C, // Magic bytes
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Padding
    0x03, 0x00, 0x00, 0x00  // Status value (3)
};

extern void bt_streaming_mock_set_connected(bool connected);

/**
 * @brief Register callback function for connection events
 */
esp_err_t bt_register_connection_callback(bt_connection_callback_t callback, void* user_data) 
{
    s_connection_callback = callback;
    s_connection_callback_data = user_data;
    return ESP_OK;
}

/**
 * @brief Register callback function for streaming events
 */
esp_err_t bt_register_streaming_callback(bt_stream_callback_t callback, void* user_data)
{
    s_stream_callback = callback;
    s_stream_callback_data = user_data;
    return ESP_OK;
}

/**
 * @brief Get the connection state
 */
int bt_get_connection_state(void)
{
    return s_connected ? 1 : 0;
}

/**
 * @brief Get the streaming state
 */
int bt_get_streaming_state(void)
{
    return s_streaming ? 1 : 0;
}

/**
 * @brief Init Bluetooth module
 */
esp_err_t bt_init(void) {
    ESP_LOGI(TAG, "Stub: Initializing Bluetooth");
    s_initialized = true;
    return ESP_OK;
}

/**
 * @brief Deinit Bluetooth module
 */
esp_err_t bt_deinit(void) {
    ESP_LOGI(TAG, "Stub: Deinitializing Bluetooth");
    s_initialized = false;
    s_connected = false;
    s_streaming = false;
    s_scanning = false;
    s_discovered_count = 0;
    return ESP_OK;
}

/**
 * @brief Check if BT is initialized
 */
bool bt_is_initialized(void) {
    return s_initialized;
}

/**
 * @brief Start scanning for Bluetooth devices
 */
esp_err_t bt_scan_start(void) {
    ESP_LOGI(TAG, "Stub: Starting Bluetooth scan");
    
    if (!s_initialized) {
        ESP_LOGW(TAG, "Cannot scan - not initialized");
        return ESP_FAIL;
    }
    
    s_scanning = true;
    
    // Simulate discovering some devices
    s_discovered_count = 3;
    
    // Add mock device 1
    strcpy((char*)s_discovered_devices[0].name, "Speaker One");
    memcpy(s_discovered_devices[0].addr, "11:22:33:44:55:66", 17);
    s_discovered_devices[0].rssi = -65;
    s_discovered_devices[0].profiles = BT_PROFILE_A2DP_SINK;
    s_discovered_devices[0].type = BT_DEVICE_TYPE_A2DP_SINK;
    
    // Add mock device 2
    strcpy((char*)s_discovered_devices[1].name, "Mock Headphones");
    memcpy(s_discovered_devices[1].addr, "22:33:44:55:66:77", 17);
    s_discovered_devices[1].rssi = -75;
    s_discovered_devices[1].profiles = BT_PROFILE_A2DP_SINK;
    s_discovered_devices[1].type = BT_DEVICE_TYPE_A2DP_SINK;
    
    // Add mock device 3
    strcpy((char*)s_discovered_devices[2].name, "BT Speaker");
    memcpy(s_discovered_devices[2].addr, "33:44:55:66:77:88", 17);
    s_discovered_devices[2].rssi = -60;
    s_discovered_devices[2].profiles = BT_PROFILE_A2DP_SINK;
    s_discovered_devices[2].type = BT_DEVICE_TYPE_A2DP_SINK;
    
    return ESP_OK;
}

/**
 * @brief Stop scanning for Bluetooth devices
 */
esp_err_t bt_scan_stop(void) {
    ESP_LOGI(TAG, "Stub: Stopping Bluetooth scan");
    s_scanning = false;
    return ESP_OK;
}

/**
 * @brief Start a filtered scan for specific device types
 */
esp_err_t bt_scan_start_filtered(bt_device_type_t device_type) {
    ESP_LOGI(TAG, "Stub: Starting filtered Bluetooth scan with filter: %d", device_type);
    
    if (!s_initialized) {
        ESP_LOGW(TAG, "Cannot scan - not initialized");
        return ESP_FAIL;
    }
    
    s_scanning = true;
    
    // Always simulate finding 2 devices for any filter
    s_discovered_count = 2;
    
    // Add mock device 1
    strcpy((char*)s_discovered_devices[0].name, "Speaker One");
    memcpy(s_discovered_devices[0].addr, "11:22:33:44:55:66", 17);
    s_discovered_devices[0].rssi = -65;
    s_discovered_devices[0].profiles = BT_PROFILE_A2DP_SINK;
    s_discovered_devices[0].type = device_type;
    
    // Add mock device 2
    strcpy((char*)s_discovered_devices[1].name, "BT Headset");
    memcpy(s_discovered_devices[1].addr, "33:44:55:66:77:88", 17);
    s_discovered_devices[1].rssi = -60;
    s_discovered_devices[1].profiles = BT_PROFILE_A2DP_SINK;
    s_discovered_devices[1].type = device_type;
    
    return ESP_OK;
}

/**
 * @brief Check if a filter returned matching devices
 */
bool bt_filter_has_matches(bt_device_type_t device_type) {
    // Always return true for tests
    return true;
}

/**
 * @brief Get count of discovered devices
 */
uint16_t bt_get_discovered_device_count(void) {
    return s_discovered_count;
}

/**
 * @brief Get list of discovered devices
 */
esp_err_t bt_get_discovered_devices(bt_device_t* devices, uint16_t max_count, uint16_t* count) {
    if (devices == NULL || max_count == 0 || count == NULL) {
        return ESP_FAIL;
    }
    
    *count = (s_discovered_count < max_count) ? s_discovered_count : max_count;
    
    for (uint16_t i = 0; i < *count; i++) {
        memcpy(&devices[i], &s_discovered_devices[i], sizeof(bt_device_t));
    }
    
    return ESP_OK;
}

/**
 * @brief Scan for devices
 */
esp_err_t bt_scan(uint32_t duration_s) {
    ESP_LOGI(TAG, "Stub: Starting scan for %" PRIu32 " seconds", duration_s);
    return bt_scan_start();
}

/**
 * @brief Connect to device by address
 */
esp_err_t bt_connect(const char* addr) {
    ESP_LOGI(TAG, "Stub: Connecting to %s", addr);
    
    if (!s_initialized) {
        ESP_LOGW(TAG, "Cannot connect - not initialized");
        return ESP_FAIL;
    }
    
    // Store the address
    strncpy(s_connected_addr, addr, sizeof(s_connected_addr) - 1);
    s_connected = true;
    
    // Update connection info
    s_current_connection.connected = true;
    strcpy(s_current_connection.remote_addr, addr);
    strcpy(s_current_connection.remote_name, s_connected_name);
    s_current_connection.connection_quality = 80; // Good quality
    
    // Notify bt_streaming_mock that we're connected
    bt_streaming_mock_set_connected(true);
    
    return ESP_OK;
}

/**
 * @brief Connect to device by name
 */
esp_err_t bt_connect_by_name(const char* name) {
    ESP_LOGI(TAG, "Stub: Connecting to device named %s", name);
    
    if (!s_initialized) {
        ESP_LOGW(TAG, "Cannot connect - not initialized");
        return ESP_FAIL;
    }
    
    // Store the name and a mock address
    strncpy(s_connected_name, name, sizeof(s_connected_name) - 1);
    strncpy(s_connected_addr, "AA:BB:CC:DD:EE:FF", sizeof(s_connected_addr) - 1);
    s_connected = true;
    
    // Update connection info
    s_current_connection.connected = true;
    strcpy(s_current_connection.remote_addr, s_connected_addr);
    strcpy(s_current_connection.remote_name, name);
    s_current_connection.connection_quality = 80; // Good quality
    
    return ESP_OK;
}

/**
 * @brief Connect to device with timeout
 */
esp_err_t bt_connect_with_timeout(const char* addr, uint32_t timeout_ms) {
    ESP_LOGI(TAG, "Stub: Connecting to %s with timeout %" PRIu32 "ms", addr, timeout_ms);
    
    if (!s_initialized) {
        ESP_LOGW(TAG, "Cannot connect - not initialized");
        return ESP_FAIL;
    }
    
    // Store the address
    strncpy(s_connected_addr, addr, sizeof(s_connected_addr) - 1);
    s_connected = true;
    
    // Update connection info
    s_current_connection.connected = true;
    strcpy(s_current_connection.remote_addr, addr);
    strcpy(s_current_connection.remote_name, "Device with timeout");
    s_current_connection.connection_quality = 80; // Good quality
    
    return ESP_OK;
}

/**
 * @brief Disconnect from device
 */
esp_err_t bt_disconnect(void) {
    ESP_LOGI(TAG, "Stub: Disconnecting");
    
    if (!s_connected) {
        ESP_LOGW(TAG, "Cannot disconnect - not connected");
        return ESP_FAIL;
    }
    
    s_connected = false;
    s_streaming = false;
    memset(s_connected_addr, 0, sizeof(s_connected_addr));
    memset(s_connected_name, 0, sizeof(s_connected_name));
    
    // Update connection info
    s_current_connection.connected = false;
    memset(s_current_connection.remote_addr, 0, sizeof(s_current_connection.remote_addr));
    memset(s_current_connection.remote_name, 0, sizeof(s_current_connection.remote_name));
    s_current_connection.connection_quality = 0;
    
    // Notify bt_streaming_mock that we're disconnected
    bt_streaming_mock_set_connected(false);
    
    return ESP_OK;
}

/**
 * @brief Check if connected
 */
bool bt_is_connected(void) {
    return s_connected;
}

/**
 * @brief Get connection info
 */
esp_err_t bt_get_connection_info(bt_connection_info_t* info) {
    if (info == NULL) {
        return ESP_FAIL;
    }
    
    // Copy connection info
    memcpy(info, &s_current_connection, sizeof(bt_connection_info_t));
    
    return ESP_OK;
}

/**
 * @brief Get paired device count
 */
uint16_t bt_get_paired_device_count(void) {
    return s_paired_count;
}

/**
 * @brief Get paired devices list
 */
void* bt_get_paired_devices(void) {
    return s_paired_devices;
}

/**
 * @brief Simulate a connection drop
 */
esp_err_t bt_simulate_disconnect(void) {
    ESP_LOGI(TAG, "Simulating remote disconnect");
    s_connected = false;
    s_streaming = false;
    
    // Update connection info
    s_current_connection.connected = false;
    
    return ESP_OK;
}
