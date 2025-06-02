/**
 * @file bt_source_mock.h
 * @brief Mock implementation of Bluetooth source functions for testing
 */

#ifndef BT_SOURCE_MOCK_H
#define BT_SOURCE_MOCK_H

#include "bt_source.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Reset the mock state
 */
void bt_mock_reset(void);

/**
 * Simulate SSP pairing request
 * 
 * @param passkey Passkey to display (6-digit number)
 */
void bt_mock_simulate_ssp_request(uint32_t passkey);

/**
 * Set whether SSP is supported
 * 
 * @param supported Whether SSP is supported
 */
void bt_mock_set_ssp_supported(bool supported);

/**
 * Add a test device to the mock
 * 
 * @param addr_str Device address string
 * @param name Device name
 * @param type Device type
 */
void bt_mock_add_test_device(const char* addr_str, const char* name, bt_device_type_t type);

/**
 * Simulate PIN pairing failure
 */
void bt_mock_simulate_pin_failure(void);

/**
 * Simulate pairing timeout
 */
void bt_mock_simulate_pairing_timeout(void);

/**
 * Set discovered devices for the mock
 * 
 * @param devices Array of devices
 * @param count Number of devices
 */
void bt_mock_set_discovered_devices(const bt_device_t* devices, int count);

/**
 * Unpair all devices
 * 
 * @return ESP_OK if successful
 */
esp_err_t bt_unpair_all_devices(void);

/**
 * Get paired device count 
 *
 * @return Number of paired devices
 */
uint16_t bt_get_paired_device_count(void);

#ifdef __cplusplus
}
#endif

#endif /* BT_SOURCE_MOCK_H */
