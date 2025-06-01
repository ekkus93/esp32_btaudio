#ifndef BT_SOURCE_MOCK_H
#define BT_SOURCE_MOCK_H

#include "../../main/bt_source.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the mock framework
 */
void bt_mock_init(void);

/**
 * @brief Reset all mock state
 */
void bt_mock_reset(void);

/**
 * @brief Set the expected return value for bt_init()
 *
 * @param ret Return value
 */
void bt_mock_set_init_return(esp_err_t ret);

/**
 * @brief Set the connected state
 * 
 * @param connected True if connected
 */
void bt_mock_set_is_connected_return(bool connected);

/**
 * @brief Set the streaming state
 * 
 * @param streaming True if streaming
 */
void bt_mock_set_is_streaming_return(bool streaming);

/**
 * @brief Set the return value for scan_start
 * 
 * @param ret Return value
 */
void bt_mock_set_scan_start_return(esp_err_t ret);

/**
 * @brief Set the return value for connect
 * 
 * @param ret Return value
 */
void bt_mock_set_connect_return(esp_err_t ret);

/**
 * @brief Setup paired devices for testing
 * 
 * @param devices Device array
 * @param count Number of devices
 */
void bt_mock_set_paired_devices(bt_device_t *devices, int count);

/**
 * @brief Set discovered devices for testing
 * 
 * @param devices Device array
 * @param count Number of devices
 */
void bt_mock_set_discovered_devices(bt_device_t *devices, int count);

/**
 * @brief Set devices by device type for filtered scan testing
 * 
 * @param type Device type to filter by
 * @param devices Device array
 * @param count Number of devices
 */
void bt_mock_set_devices_by_type(bt_device_type_t type, bt_device_t *devices, int count);

/**
 * @brief Simulate a scan timeout
 */
void bt_mock_simulate_timeout(void);

/**
 * @brief Simulate a connection drop
 */
void bt_mock_simulate_disconnect(void);

/**
 * @brief Simulate a reconnection
 */
void bt_mock_simulate_reconnect(void);

/**
 * @brief Set the timeout return value for connection
 * 
 * @param ret Return value
 */
void bt_mock_set_connect_timeout_return(esp_err_t ret);

/**
 * @brief Set connection info for testing
 *
 * @param addr Remote device address
 * @param name Remote device name
 * @param rssi Signal strength
 */
void bt_mock_set_connection_info(const char* addr, const char* name, int8_t rssi);

/**
 * @brief Set the streaming state for the mock
 * 
 * @param state The streaming state to set
 */
void bt_mock_set_streaming_state(bt_streaming_state_t state);

/**
 * @brief Get the currently active profile in use
 * 
 * @return Active profile bit mask
 */
bt_profile_t bt_mock_get_active_profile(void);

/**
 * @brief Set the active profile
 * 
 * @param profile Profile bit mask
 */
void bt_mock_set_active_profile(bt_profile_t profile);

#ifdef __cplusplus
}
#endif

#endif /* BT_SOURCE_MOCK_H */
