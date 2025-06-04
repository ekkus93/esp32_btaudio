#ifndef BT_MOCK_DEVICES_H
#define BT_MOCK_DEVICES_H

#include "esp_err.h"
#include "bt_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Add a mock BT device
 * @param addr Device MAC address as string
 * @param name Device name
 * @param type Device type
 * @param supports_a2dp Whether the device supports A2DP
 */
void bt_mock_add_device(const char* addr, const char* name, bt_device_type_t type, bool supports_a2dp);

/**
 * @brief Start BT scan in mock
 */
void bt_mock_start_scan(void);

/**
 * @brief Stop BT scan in mock
 */
void bt_mock_stop_scan(void);

/**
 * @brief Get scan results
 * @param devices Pointer to array of devices to fill
 * @param max_count Maximum number of devices to return
 * @return Number of devices actually returned
 */
int bt_mock_get_scan_results(bt_device_t* devices, int max_count);

/**
 * @brief Connect to device in mock
 * @param addr Device address
 * @return ESP_OK on success
 */
esp_err_t bt_mock_connect(const char* addr);

/**
 * @brief Disconnect from device in mock
 * @return ESP_OK on success
 */
esp_err_t bt_mock_disconnect(void);

/**
 * @brief Check if connected in mock
 * @return true if connected
 */
bool bt_mock_is_connected(void);

/**
 * @brief Get connected device address
 * @return Address of connected device, or NULL if not connected
 */
const char* bt_mock_get_connected_addr(void);

/**
 * @brief Start pairing in mock
 * @param addr Device address
 * @return ESP_OK on success
 */
esp_err_t bt_mock_start_pairing(const char* addr);

/**
 * @brief Get pairing state in mock
 * @return Current pairing state
 */
bt_pairing_state_t bt_mock_get_pairing_state(void);

/**
 * @brief Get pairing method in mock
 * @return Current pairing method
 */
bt_pairing_method_t bt_mock_get_pairing_method(void);

/**
 * @brief Send PIN in mock
 * @param pin PIN code string
 * @return ESP_OK on success
 */
esp_err_t bt_mock_send_pin(const char* pin);

/**
 * @brief Check if confirmation requested in mock
 * @return true if confirmation requested
 */
bool bt_mock_is_ssp_confirm_requested(void);

/**
 * @brief Confirm SSP pairing in mock
 * @param confirm true to confirm, false to reject
 * @return ESP_OK on success
 */
esp_err_t bt_mock_confirm_ssp(bool confirm);

/**
 * @brief Set default PIN in mock
 * @param pin Default PIN
 * @return ESP_OK on success
 */
esp_err_t bt_mock_set_default_pin(const char* pin);

/**
 * @brief Add paired device in mock
 * @param device Device to add
 * @return ESP_OK on success
 */
esp_err_t bt_mock_add_paired_device(const bt_device_t* device);

/**
 * @brief Unpair device in mock
 * @param addr Device address
 * @return ESP_OK on success
 */
esp_err_t bt_mock_unpair_device(const char* addr);

/**
 * @brief Unpair all devices in mock
 * @return ESP_OK on success
 */
esp_err_t bt_mock_unpair_all_devices(void);

/**
 * @brief Get paired device count in mock
 * @return Number of paired devices
 */
int bt_mock_get_paired_device_count(void);

/**
 * @brief Get paired devices in mock
 * @param devices Array to store devices
 * @param max_count Maximum number to retrieve
 * @return Number of devices retrieved
 */
int bt_mock_get_paired_devices(bt_device_t* devices, int max_count);

/**
 * @brief Check if device is paired in mock
 * @param addr Device address
 * @return true if paired
 */
bool bt_mock_is_device_paired(const char* addr);

/**
 * @brief Reset mock state
 */
void bt_mock_reset(void);

/**
 * @brief Clean up all mock resources
 */
void bt_mock_cleanup(void);

/**
 * @brief Set whether SSP is supported
 * @param supported True if SSP is supported, false otherwise
 */
void bt_mock_set_ssp_supported(bool supported);

/**
 * @brief Simulate an SSP request with the given passkey
 * @param passkey The numeric passkey to display
 * @return ESP_OK on success
 */
esp_err_t bt_mock_simulate_ssp_request(uint32_t passkey);

/**
 * @brief Get the current SSP passkey
 * @return The current SSP passkey
 */
uint32_t bt_mock_get_ssp_passkey(void);

#ifdef __cplusplus
}
#endif

#endif /* BT_MOCK_DEVICES_H */
