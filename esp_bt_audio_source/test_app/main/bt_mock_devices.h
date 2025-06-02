#ifndef BT_MOCK_DEVICES_H
#define BT_MOCK_DEVICES_H

#include <stdbool.h>
#include "esp_err.h"
#include "bt_source.h"

/**
 * @brief Reset all mock device data
 */
void bt_mock_reset(void);

/**
 * @brief Add a mock device for testing
 * 
 * @param addr Bluetooth address in format "XX:XX:XX:XX:XX:XX"
 * @param name Device name
 * @param type Device type
 * @param supports_a2dp Whether device supports A2DP profile
 */
void bt_mock_add_device(const char* addr, const char* name, bt_device_type_t type, bool supports_a2dp);

/**
 * @brief Start a mock Bluetooth scan
 */
void bt_mock_start_scan(void);

/**
 * @brief Stop the mock Bluetooth scan
 */
void bt_mock_stop_scan(void);

/**
 * @brief Get the results from a mock scan
 * 
 * @param devices Array to populate with discovered devices
 * @param max_count Maximum number of devices to return
 * @return Number of devices found
 */
int bt_mock_get_scan_results(bt_device_t* devices, int max_count);

/**
 * @brief Connect to a mock device
 * 
 * @param addr Device address in format "XX:XX:XX:XX:XX:XX"
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_mock_connect(const char* addr);

/**
 * @brief Disconnect from current mock device
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_mock_disconnect(void);

/**
 * @brief Check if connected to a mock device
 * 
 * @return true if connected, false otherwise
 */
bool bt_mock_is_connected(void);

/**
 * @brief Get the connected device address
 * 
 * @return Address string or NULL if not connected
 */
const char* bt_mock_get_connected_addr(void);

/**
 * @brief Safely copy the connected device address to a buffer
 * 
 * @param addr_buf Buffer to store the address
 * @param buf_size Size of the buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_mock_copy_connected_addr(char* addr_buf, size_t buf_size);

/**
 * @brief Set the default PIN code for pairing
 * 
 * @param pin PIN code string
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_mock_set_default_pin(const char* pin);

/**
 * @brief Get the current pairing state
 * 
 * @return Current pairing state
 */
bt_pairing_state_t bt_mock_get_pairing_state(void);

/**
 * @brief Get the current pairing method
 * 
 * @return Current pairing method
 */
bt_pairing_method_t bt_mock_get_pairing_method(void);

/**
 * @brief Enable or disable SSP support
 * 
 * @param supported Whether SSP is supported
 */
void bt_mock_set_ssp_supported(bool supported);

/**
 * @brief Simulate an SSP request with a passkey
 * 
 * @param passkey Passkey to use for SSP
 */
void bt_mock_simulate_ssp_request(uint32_t passkey);

/**
 * @brief Check if an SSP confirmation is requested
 * 
 * @return true if confirmation is requested, false otherwise
 */
bool bt_mock_is_ssp_confirm_requested(void);

/**
 * @brief Get the current SSP passkey
 * 
 * @return SSP passkey value
 */
uint32_t bt_mock_get_ssp_passkey(void);

#endif /* BT_MOCK_DEVICES_H */
