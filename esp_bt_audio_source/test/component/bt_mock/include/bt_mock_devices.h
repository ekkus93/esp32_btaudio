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

/* Connection and scan helpers (ensure these higher-level device helpers are
 * declared in this component header so translation units that include the
 * bt_mock component header (which appears earlier on the include path)
 * get the full set of prototypes and do not trigger implicit declaration
 * warnings/errors.
 */
esp_err_t bt_mock_connect(const char* addr);
esp_err_t bt_mock_disconnect(void);
bool bt_mock_is_connected(void);
void bt_mock_set_connect_by_name_hook(const char* name, const char* addr);
esp_err_t bt_mock_hook_connect_by_name(const char* name);

/* Scan helpers */
void bt_mock_start_scan(void);
void bt_mock_stop_scan(void);
int bt_mock_get_scan_results(bt_device_t* devices, int max_count);
bool bt_mock_is_scanning(void);

/* Connection info helpers -------------------------------------------------*/
/**
 * @brief Get the currently connected device address (string)
 *
 * @param buf Buffer to fill with address string ("XX:XX:...")
 * @param len Buffer length
 * @return ESP_OK if connected and buffer filled, ESP_ERR_NOT_FOUND if not connected
 */
esp_err_t bt_mock_get_connected_addr(char* buf, size_t len);

/**
 * @brief Get the component mock's streaming state
 *
 * @return bt_streaming_state_t current streaming state
 */
bt_streaming_state_t bt_mock_get_streaming_state(void);

/* Test-only helper: force authoritative disconnect immediately.
 * This synchronously clears the component mock's connected flag and
 * connected address. Use only from test code (stubs/tests) when a
 * polling timeout occurs to deterministically recover from races.
 */
esp_err_t bt_mock_force_disconnect(void);

/* Pairing and SSP helpers used by tests */
bt_pairing_state_t bt_mock_get_pairing_state(void);
bt_pairing_method_t bt_mock_get_pairing_method(void);
esp_err_t bt_mock_simulate_ssp_request(uint32_t passkey);
bool bt_mock_is_ssp_confirm_requested(void);
esp_err_t bt_mock_confirm_ssp(bool confirm);
esp_err_t bt_mock_get_ssp_passkey(char* passkey, size_t size);

/* Simulate a pairing timeout (test-only helper implemented in bt_mock_devices.c)
 * This affects the authoritative pairing state used by tests.
 */
void bt_mock_devices_simulate_pairing_timeout(void);

/* Paired device management (authoritative signatures) */
esp_err_t bt_mock_add_paired_device(bt_device_t* device);
uint16_t bt_mock_get_paired_device_count(void);
esp_err_t bt_mock_get_paired_devices(bt_device_t *devices, uint16_t max_count, uint16_t *actual_count);
esp_err_t bt_mock_unpair_all_devices(void);

/* Default PIN helpers */
esp_err_t bt_mock_set_default_pin(const char* pin);
esp_err_t bt_mock_get_default_pin(char* pin, size_t size);
#ifdef __cplusplus
}
#endif