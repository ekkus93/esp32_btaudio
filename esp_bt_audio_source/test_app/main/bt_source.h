#ifndef BT_SOURCE_H
#define BT_SOURCE_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Bluetooth device structure
 */
typedef struct {
    uint8_t addr[6];        // Bluetooth address (MAC)
    char name[32];          // Device name
    int rssi;               // Signal strength
    uint8_t cod;            // Class of Device
    bool paired;            // Paired status
} bt_device_t;

/**
 * @brief Bluetooth profile types
 */
typedef enum {
    BT_PROFILE_A2DP_SINK = 0,
    BT_PROFILE_A2DP_SOURCE,
    BT_PROFILE_HFP,
    BT_PROFILE_AVRCP
} bt_profile_t;

/**
 * @brief Bluetooth device types
 */
typedef enum {
    BT_DEVICE_TYPE_UNKNOWN = 0,
    BT_DEVICE_TYPE_CLASSIC = 1,
    BT_DEVICE_TYPE_BLE = 2,
    BT_DEVICE_TYPE_A2DP_SINK = 3,
    BT_DEVICE_TYPE_A2DP_SOURCE = 4
} bt_device_type_t;

/**
 * @brief Bluetooth discovery callback type
 */
typedef void (*bt_discovery_cb_t)(bt_device_t* device, void* user_data);

/**
 * @brief Initialize Bluetooth stack
 */
esp_err_t bt_init(void);

/**
 * @brief Start scanning for Bluetooth devices
 */
esp_err_t bt_scan_start(void);

/**
 * @brief Start scanning with filter
 */
esp_err_t bt_scan_start_filtered(bt_device_type_t device_type);

/**
 * @brief Stop scanning for Bluetooth devices
 */
esp_err_t bt_scan_stop(void);

/**
 * @brief Get number of discovered devices
 */
uint16_t bt_get_discovered_device_count(void);

/**
 * @brief Get discovered devices
 */
esp_err_t bt_get_discovered_devices(bt_device_t* devices, int max_count, uint16_t* device_count);

/**
 * @brief Connect to a Bluetooth device
 */
esp_err_t bt_connect(const char* address);

/**
 * @brief Check if connected to any device
 */
bool bt_is_connected(void);

/**
 * @brief Start A2DP audio streaming
 */
esp_err_t bt_start_streaming(void);

/**
 * @brief Check if A2DP is currently streaming
 */
bool bt_is_streaming(void);

/**
 * @brief Stop A2DP audio streaming
 */
esp_err_t bt_stop_streaming(void);

/**
 * @brief Disconnect from the currently connected device
 */
esp_err_t bt_disconnect(void);

/**
 * @brief Get number of paired devices
 */
uint16_t bt_get_paired_device_count(void);

/**
 * @brief Add a device to the paired devices list
 */
esp_err_t bt_add_paired_device(bt_device_t* device);

/**
 * @brief Remove a device from the paired devices list
 */
esp_err_t bt_remove_paired_device(bt_device_t* device);

/**
 * @brief Register a callback for device discovery events
 */
esp_err_t bt_register_discovery_callback(bt_discovery_cb_t callback, void* user_data);

/**
 * @brief Check if device supports a specific profile
 */
bool bt_device_supports_profile(const bt_device_t* device, bt_profile_t profile);

#endif /* BT_SOURCE_H */
