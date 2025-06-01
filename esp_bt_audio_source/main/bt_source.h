#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BT device type enum
 */
typedef enum {
    BT_DEVICE_TYPE_UNKNOWN = 0,
    BT_DEVICE_TYPE_CLASSIC,
    BT_DEVICE_TYPE_BLE,
    BT_DEVICE_TYPE_DUAL
} bt_device_type_t;

/**
 * @brief BT profile type enum
 */
typedef enum {
    BT_PROFILE_NONE = 0,
    BT_PROFILE_A2DP_SINK = (1 << 0),
    BT_PROFILE_A2DP_SOURCE = (1 << 1),
    BT_PROFILE_HFP = (1 << 2),
    BT_PROFILE_SPP = (1 << 3)
} bt_profile_t;

/**
 * @brief BT device structure
 */
typedef struct {
    uint8_t addr[6];           // Device address
    char name[32];             // Device name
    int8_t rssi;               // Signal strength
    bool paired;               // Is device paired
    uint32_t cod;              // Class of Device
} bt_device_t;

/**
 * @brief Connection information structure
 */
typedef struct {
    bool connected;             // Whether currently connected
    char remote_addr[18];       // Remote device address string
    char remote_name[32];       // Remote device name
    int8_t signal_strength;     // Signal strength (RSSI)
} bt_connection_info_t;

/**
 * @brief Discovery callback function type
 */
typedef void (*bt_discovery_cb_t)(bt_device_t *device, void *user_data);

/**
 * @brief Connection state change callback type
 */
typedef void (*bt_connection_callback_t)(bool connected, bt_device_t* device, esp_err_t status, void* user_data);

/**
 * @brief Initialize Bluetooth stack
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_init(void);

/**
 * @brief Start BT scanning
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_scan_start(void);

/**
 * @brief Start filtered scan for specific device type
 * 
 * @param device_type Device type to filter for
 * @return ESP_OK on success
 */
esp_err_t bt_scan_start_filtered(bt_device_type_t device_type);

/**
 * @brief Stop scanning
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_scan_stop(void);

/**
 * @brief Connect to a device
 * 
 * @param addr MAC address string in format "XX:XX:XX:XX:XX:XX"
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_connect(const char* addr);

/**
 * @brief Connect to a device by name
 * 
 * @param name Device name to connect to
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_connect_by_name(const char* name);

/**
 * @brief Connect with timeout
 * 
 * @param addr MAC address string in format "XX:XX:XX:XX:XX:XX"
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_connect_with_timeout(const char* addr, uint32_t timeout_ms);

/**
 * @brief Check if connected to a BT device
 * 
 * @return true if connected, false otherwise
 */
bool bt_is_connected(void);

/**
 * @brief Disconnect from current device
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_disconnect(void);

/**
 * @brief Start audio streaming
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_start_streaming(void);

/**
 * @brief Stop audio streaming
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_stop_streaming(void);

/**
 * @brief Check if streaming is active
 * 
 * @return true if streaming, false otherwise
 */
bool bt_is_streaming(void);

/**
 * @brief Register device discovery callback
 * 
 * @param callback Function to call when device is discovered
 * @param user_data User data to pass to callback
 * @return ESP_OK on success
 */
esp_err_t bt_register_discovery_callback(bt_discovery_cb_t callback, void *user_data);

/**
 * @brief Get discovered device count
 * 
 * @return Number of discovered devices
 */
uint16_t bt_get_discovered_device_count(void);

/**
 * @brief Get discovered devices
 * 
 * @param devices Array to store devices (must be allocated by caller)
 * @param max_count Maximum number of devices to store
 * @param device_count Actual number of devices stored
 * @return ESP_OK on success
 */
esp_err_t bt_get_discovered_devices(bt_device_t* devices, int max_count, uint16_t* device_count);

/**
 * @brief Get paired device count
 * 
 * @return Number of paired devices
 */
uint16_t bt_get_paired_device_count(void);

/**
 * @brief Add paired device
 * 
 * @param device Device to add
 * @return ESP_OK on success
 */
esp_err_t bt_add_paired_device(bt_device_t* device);

/**
 * @brief Remove paired device
 * 
 * @param device Device to remove
 * @return ESP_OK on success
 */
esp_err_t bt_remove_paired_device(bt_device_t* device);

/**
 * @brief Register callback for connection state changes
 * 
 * @param callback Function to call when connection state changes
 * @param user_data User data to pass to callback
 * @return ESP_OK on success
 */
esp_err_t bt_register_connection_callback(bt_connection_callback_t callback, void* user_data);

/**
 * @brief Get current connection information
 * 
 * @param info Pointer to connection info structure to fill
 * @return ESP_OK on success
 */
esp_err_t bt_get_connection_info(bt_connection_info_t* info);

/**
 * @brief Enable or disable auto-reconnection
 * 
 * @param enable True to enable auto-reconnect, false to disable
 * @return ESP_OK on success
 */
esp_err_t bt_set_auto_reconnect(bool enable);

/**
 * @brief Simulate disconnection (for testing only)
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_simulate_disconnect(void);

/**
 * @brief Check if device supports a profile
 * 
 * @param device Device to check
 * @param profile Profile to check for
 * @return true if supported, false otherwise
 */
bool bt_device_supports_profile(const bt_device_t* device, bt_profile_t profile);

/**
 * @brief Start BT scanning with timeout
 * 
 * @param duration_s Duration in seconds
 * @return ESP_OK on success
 */
esp_err_t bt_scan(uint32_t duration_s);

/**
 * @brief Check if BT is currently scanning
 * 
 * @return true if scanning, false otherwise
 */
bool bt_is_scanning(void);

/**
 * @brief Pause streaming audio
 * 
 * @return ESP_OK on success, ESP_FAIL if not streaming
 */
esp_err_t bt_pause_streaming(void);

/**
 * @brief Resume previously paused audio streaming
 * 
 * @return ESP_OK on success, ESP_FAIL if not paused
 */
esp_err_t bt_resume_streaming(void);

/**
 * @brief Bluetooth streaming states
 */
typedef enum {
    BT_STREAMING_STATE_STOPPED = 0,
    BT_STREAMING_STATE_PLAYING,
    BT_STREAMING_STATE_PAUSED
} bt_streaming_state_t;

/**
 * @brief Check if streaming is paused
 * 
 * @return true if paused, false otherwise
 */
bool bt_is_paused(void);

/**
 * @brief Get current streaming state
 * 
 * @return Current streaming state (STOPPED, PLAYING, PAUSED)
 */
bt_streaming_state_t bt_get_streaming_state(void);

#ifdef __cplusplus
}
#endif
