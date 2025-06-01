#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Device types for filtering
 */
typedef enum {
    BT_DEVICE_TYPE_ANY = 0,
    BT_DEVICE_TYPE_A2DP_SINK,
    BT_DEVICE_TYPE_HFP,
    // Add more types as needed
} bt_device_type_t;

/**
 * @brief Bluetooth profiles
 */
typedef enum {
    BT_PROFILE_A2DP_SINK = 0x01,
    BT_PROFILE_HFP = 0x02,
    // Add more profiles as needed
} bt_profile_t;

/**
 * @brief Bluetooth device information structure
 */
typedef struct {
    uint8_t addr[6];           // Device MAC address
    char name[32];             // Device name
    int8_t rssi;               // Signal strength
    bool paired;               // Whether device is paired
    bool connected;            // Whether device is connected
    uint16_t profiles;         // Supported profiles (bit mask)
} bt_device_t;

/**
 * @brief Callback for device discovery events
 * 
 * @param device Pointer to discovered device info
 * @param user_data User data passed during registration
 */
typedef void (*bt_discovery_cb_t)(bt_device_t* device, void* user_data);

/**
 * @brief Initialize the Bluetooth stack and A2DP source profile
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_init(void);

/**
 * @brief Start scanning for Bluetooth devices
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_scan_start(void);

/**
 * @brief Stop scanning for Bluetooth devices
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_scan_stop(void);

/**
 * @brief Get number of discovered devices during scan
 * 
 * @return Number of discovered devices
 */
uint16_t bt_get_discovered_device_count(void);

/**
 * @brief Connect to a Bluetooth device by MAC address
 * 
 * @param addr MAC address string in format "XX:XX:XX:XX:XX:XX"
 * @return ESP_OK on success
 */
esp_err_t bt_connect(const char* addr);

/**
 * @brief Check if connected to a Bluetooth device
 * 
 * @return true if connected, false otherwise
 */
bool bt_is_connected(void);

/**
 * @brief Disconnect from connected Bluetooth device
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_disconnect(void);

/**
 * @brief Start A2DP audio streaming
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_start_streaming(void);

/**
 * @brief Stop A2DP audio streaming
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_stop_streaming(void);

/**
 * @brief Check if audio is currently streaming
 * 
 * @return true if streaming, false otherwise
 */
bool bt_is_streaming(void);

/**
 * @brief Get the number of paired devices in memory
 * 
 * @return Number of paired devices
 */
uint16_t bt_get_paired_device_count(void);

/**
 * @brief Add a device to the paired devices list
 * 
 * @param device Pointer to device information structure
 * @return ESP_OK on success
 */
esp_err_t bt_add_paired_device(const bt_device_t* device);

/**
 * @brief Remove a device from the paired devices list
 * 
 * @param device Pointer to device information structure
 * @return ESP_OK on success
 */
esp_err_t bt_remove_paired_device(const bt_device_t* device);

/**
 * @brief Register callback for device discovery events
 * 
 * @param callback Function to call when device is discovered
 * @param user_data User data to pass to callback
 * @return ESP_OK on success
 */
esp_err_t bt_register_discovery_callback(bt_discovery_cb_t callback, void* user_data);

/**
 * @brief Start scanning with device type filter
 * 
 * @param device_type Type of device to filter for
 * @return ESP_OK on success
 */
esp_err_t bt_scan_start_filtered(bt_device_type_t device_type);

/**
 * @brief Get list of discovered devices
 * 
 * @param devices Array to store discovered devices
 * @param max_count Maximum number of devices to return
 * @param count Actual number of devices returned
 * @return ESP_OK on success
 */
esp_err_t bt_get_discovered_devices(bt_device_t* devices, uint16_t max_count, uint16_t* count);

/**
 * @brief Check if device supports specific profile
 * 
 * @param device Device to check
 * @param profile Profile to check for
 * @return true if device supports profile
 */
bool bt_device_supports_profile(const bt_device_t* device, bt_profile_t profile);

#ifdef __cplusplus
}
#endif
