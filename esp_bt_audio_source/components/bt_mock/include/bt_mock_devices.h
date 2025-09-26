/**
 * @file bt_mock_devices.h
 * @brief Mock implementation of Bluetooth device management for testing
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "bt_source.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the mock device system
 */
void bt_mock_devices_init(void);

/**
 * @brief Clean up the mock device system
 */
void bt_mock_devices_cleanup(void);

/**
 * @brief Reset all mock devices
 */
void bt_mock_devices_reset(void);

/**
 * @brief Get count of mock devices
 * 
 * @return Count of devices
 */
int bt_mock_devices_count(void);

/**
 * @brief Add a mock device with parameters
 * 
 * @param addr_str Device address string
 * @param name Device name
 * @param type Device type
 * @param paired Whether device is paired
 * @return ESP_OK on success
 */
esp_err_t bt_mock_add_device(const char* addr_str, const char* name, bt_device_type_t type, bool paired);

/**
 * @brief Get device information by index
 * 
 * @param index Device index
 * @param device Device information to populate
 * @return ESP_OK on success
 */
esp_err_t bt_mock_get_device(int index, bt_device_t* device);

/**
 * @brief Remove a mock device by index
 * 
 * @param index Device index
 * @return ESP_OK on success
 */
esp_err_t bt_mock_remove_device(int index);

/**
 * @brief Find a device by address
 * 
 * @param addr_str Address string in format "XX:XX:XX:XX:XX:XX"
 * @param index Output parameter to store index if found
 * @return true if found, false otherwise
 */
bool bt_mock_find_device(const char* addr_str, int* index);

#ifdef __cplusplus
}
#endif