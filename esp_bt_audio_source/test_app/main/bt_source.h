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
    BT_DEVICE_TYPE_DUAL,
    BT_DEVICE_TYPE_ANY,       // For scan filtering - match any device type
    BT_DEVICE_TYPE_A2DP_SINK  // For scan filtering - match A2DP sink devices
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
    bool connected;            // Is device currently connected
    uint32_t profiles;         // Supported profiles (bitmask of bt_profile_t)
    uint32_t cod;              // Class of Device - for backward compatibility
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
 */
esp_err_t bt_init(void);

/**
 * @brief Start BT scanning
 */
esp_err_t bt_scan_start(void);

/**
 * @brief Start filtered scan for specific device type
 */
esp_err_t bt_scan_start_filtered(bt_device_type_t device_type);

/**
 * @brief Stop scanning
 */
esp_err_t bt_scan_stop(void);

/**
 * @brief Connect to a device
 */
esp_err_t bt_connect(const char* addr);

/**
 * @brief Connect to a device by name
 */
esp_err_t bt_connect_by_name(const char* name);

/**
 * @brief Connect with timeout
 */
esp_err_t bt_connect_with_timeout(const char* addr, uint32_t timeout_ms);

/**
 * @brief Check if connected to a BT device
 */
bool bt_is_connected(void);

/**
 * @brief Disconnect from current device
 */
esp_err_t bt_disconnect(void);

/**
 * @brief Start audio streaming
 */
esp_err_t bt_start_streaming(void);

/**
 * @brief Stop audio streaming
 */
esp_err_t bt_stop_streaming(void);

/**
 * @brief Check if streaming is active
 */
bool bt_is_streaming(void);

/**
 * @brief Register device discovery callback
 */
esp_err_t bt_register_discovery_callback(bt_discovery_cb_t callback, void *user_data);

/**
 * @brief Get discovered device count
 */
uint16_t bt_get_discovered_device_count(void);

/**
 * @brief Get discovered devices
 */
esp_err_t bt_get_discovered_devices(bt_device_t* devices, uint16_t max_count, uint16_t* count);

/**
 * @brief Get paired device count
 */
uint16_t bt_get_paired_device_count(void);

/**
 * @brief Add paired device
 */
esp_err_t bt_add_paired_device(const bt_device_t* device);

/**
 * @brief Remove paired device
 */
esp_err_t bt_remove_paired_device(const bt_device_t* device);

/**
 * @brief Register callback for connection state changes
 */
esp_err_t bt_register_connection_callback(bt_connection_callback_t callback, void* user_data);

/**
 * @brief Get current connection information
 */
esp_err_t bt_get_connection_info(bt_connection_info_t* info);

/**
 * @brief Enable or disable auto-reconnection
 */
esp_err_t bt_set_auto_reconnect(bool enable);

/**
 * @brief Simulate disconnection (for testing only)
 */
esp_err_t bt_simulate_disconnect(void);

/**
 * @brief Check if device supports a profile
 */
bool bt_device_supports_profile(const bt_device_t* device, bt_profile_t profile);

#ifdef __cplusplus
}
#endif
