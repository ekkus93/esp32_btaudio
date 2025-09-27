/**
 * @file bt_mock.h
 * @brief Bluetooth mock functionality for testing
 */
#pragma once

/* Indicate this header provides bt_mock_* prototypes so test headers can avoid
 * defining conflicting inline wrappers or macros. Test-only headers should
 * check this macro before providing aliases for the same names.
 */
#define BT_MOCK_PROVIDES_PROTOTYPES 1

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "bt_mock_devices.h"
#include "bt_source.h" // This is needed to get bt_device_t definition

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Bluetooth mock system
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_mock_init(void);

/**
 * @brief Clean up the Bluetooth mock system
 */
void bt_mock_cleanup(void);

/**
 * @brief Reset all mock state for testing
 */
void bt_mock_reset(void);

/**
 * @brief Add a test device to the mock Bluetooth system
 * 
 * @param addr_str MAC address string in format "XX:XX:XX:XX:XX:XX"
 * @param name Device name
 * @param type Device type
 */
void bt_mock_add_test_device(const char* addr_str, const char* name, bt_device_type_t type);

/**
 * @brief Set whether SSP is supported
 * 
 * @param supported True if SSP should be supported
 */
void bt_mock_set_ssp_supported(bool supported);

/**
 * @brief Simulate pairing failure
 */
void bt_mock_simulate_pin_failure(void);

/**
 * @brief Simulate pairing timeout
 */
void bt_mock_simulate_pairing_timeout(void);

/**
 * @brief Check if a device filter has matches
 * 
 * @param timeout Search timeout in seconds
 * @return True if there are matches
 */
bool bt_filter_has_matches(int timeout);

/**
 * @brief Send SSP confirmation
 * 
 * @param confirm True to confirm, false to reject
 * @return ESP_OK on success
 */
esp_err_t bt_ssp_confirm(bool confirm);

/**
 * @brief Add a paired device to the mock system
 * 
 * @param device Device to be added as paired
 * @return ESP_OK on success
 */
esp_err_t bt_mock_add_paired_device(bt_device_t* device);

/**
 * @brief Get count of paired devices in mock system
 * 
 * @return Number of paired devices
 */
uint16_t bt_mock_get_paired_device_count(void);

/**
 * @brief Get paired devices from mock system
 * 
 * @param devices Array to store device information
 * @param max_count Maximum number of devices to retrieve
 * @param actual_count Actual number of devices retrieved
 * @return ESP_OK on success
 */
esp_err_t bt_mock_get_paired_devices(bt_device_t *devices, uint16_t max_count, uint16_t *actual_count);

#ifdef __cplusplus
}
#endif
