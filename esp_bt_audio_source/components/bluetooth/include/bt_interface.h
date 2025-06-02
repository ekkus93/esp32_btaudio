/**
 * @file bt_interface.h
 * @brief Bluetooth interface for ESP32 A2DP source
 */

#ifndef _BT_INTERFACE_H_
#define _BT_INTERFACE_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_bt_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bluetooth device information
 */
typedef struct {
    esp_bd_addr_t addr;
    char name[32];
    bool supports_a2dp;
} bt_device_info_t;

/**
 * @brief Bluetooth streaming state
 */
typedef enum {
    BT_STREAM_STATE_IDLE,
    BT_STREAM_STATE_STARTING,
    BT_STREAM_STATE_STREAMING,
    BT_STREAM_STATE_PAUSED,
    BT_STREAM_STATE_STOPPING
} bt_stream_state_t;

/**
 * @brief Bluetooth pairing state
 */
typedef enum {
    BT_PAIRING_STATE_IDLE,
    BT_PAIRING_STATE_STARTED,
    BT_PAIRING_STATE_PIN_REQUESTED,
    BT_PAIRING_STATE_SSP_REQUESTED,
    BT_PAIRING_STATE_PAIRED,
    BT_PAIRING_STATE_FAILED
} bt_pairing_state_t;

/**
 * @brief Bluetooth pairing method
 */
typedef enum {
    BT_PAIRING_METHOD_NONE,
    BT_PAIRING_METHOD_PIN,
    BT_PAIRING_METHOD_SSP
} bt_pairing_method_t;

/**
 * @brief Initialize Bluetooth subsystem
 * @return ESP_OK on success
 */
esp_err_t bt_init(void);

/**
 * @brief Deinitialize Bluetooth subsystem
 * @return ESP_OK on success
 */
esp_err_t bt_deinit(void);

/**
 * @brief Connect to a Bluetooth device
 * @param addr Bluetooth address of the device
 * @return ESP_OK on success
 */
esp_err_t bt_connect(esp_bd_addr_t addr);

/**
 * @brief Disconnect from the currently connected device
 * @return ESP_OK on success
 */
esp_err_t bt_disconnect(void);

/**
 * @brief Check if a device is connected
 * @return true if connected
 */
bool bt_is_connected(void);

/**
 * @brief Get information about the current connection
 * @param info Pointer to store connection information
 * @return ESP_OK on success
 */
esp_err_t bt_get_connection_info(bt_device_info_t *info);

/**
 * @brief Start scanning for Bluetooth devices
 * @return ESP_OK on success
 */
esp_err_t bt_scan_start(void);

/**
 * @brief Start scanning for Bluetooth devices with filtering
 * @param device_type Type of devices to filter for (e.g. "A2DP")
 * @return ESP_OK on success
 */
esp_err_t bt_scan_start_filtered(const char *device_type);

/**
 * @brief Stop scanning for Bluetooth devices
 * @return ESP_OK on success
 */
esp_err_t bt_scan_stop(void);

/**
 * @brief Check if scanning is currently active
 * @return true if scanning
 */
bool bt_is_scanning(void);

/**
 * @brief Get the number of discovered devices
 * @return Number of discovered devices
 */
int bt_get_discovered_device_count(void);

/**
 * @brief Get discovered devices
 * @param devices Array to store device information
 * @param count Number of devices to return
 * @return Number of devices found
 */
int bt_get_discovered_devices(bt_device_info_t *devices, int count);

/**
 * @brief Connect to a device by name
 * @param name Name of the device
 * @return ESP_OK on success
 */
esp_err_t bt_connect_by_name(const char *name);

/**
 * @brief Connect to a device with timeout
 * @param addr Device address
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success
 */
esp_err_t bt_connect_with_timeout(esp_bd_addr_t addr, int timeout_ms);

/**
 * @brief Set auto reconnect flag
 * @param enable True to enable auto reconnect
 * @return ESP_OK on success
 */
esp_err_t bt_set_auto_reconnect(bool enable);

/**
 * @brief Simulate a disconnect event for testing
 * @return ESP_OK on success
 */
esp_err_t bt_simulate_disconnect(void);

/**
 * @brief Start A2DP audio streaming
 * @return ESP_OK on success
 */
esp_err_t bt_start_streaming(void);

/**
 * @brief Stop A2DP audio streaming
 * @return ESP_OK on success
 */
esp_err_t bt_stop_streaming(void);

/**
 * @brief Pause A2DP audio streaming
 * @return ESP_OK on success
 */
esp_err_t bt_pause_streaming(void);

/**
 * @brief Resume A2DP audio streaming
 * @return ESP_OK on success
 */
esp_err_t bt_resume_streaming(void);

/**
 * @brief Check if streaming is active
 * @return true if streaming
 */
bool bt_is_streaming(void);

/**
 * @brief Check if streaming is paused
 * @return true if paused
 */
bool bt_is_paused(void);

/**
 * @brief Get the current streaming state
 * @return Current streaming state
 */
bt_stream_state_t bt_get_streaming_state(void);

/**
 * @brief Check if a device supports a specific profile
 * @param addr Device address
 * @param profile_id Profile ID
 * @return true if the device supports the profile
 */
bool bt_device_supports_profile(esp_bd_addr_t addr, uint16_t profile_id);

/**
 * @brief Start pairing with a device
 * @param addr Device address
 * @return ESP_OK on success
 */
esp_err_t bt_start_pairing(esp_bd_addr_t addr);

/**
 * @brief Check if a device is paired
 * @param addr Device address
 * @return true if paired
 */
bool bt_is_device_paired(esp_bd_addr_t addr);

/**
 * @brief Get the current pairing state
 * @return Current pairing state
 */
bt_pairing_state_t bt_get_pairing_state(void);

/**
 * @brief Get the current pairing method
 * @return Current pairing method
 */
bt_pairing_method_t bt_get_pairing_method(void);

/**
 * @brief Send PIN code for pairing
 * @param pin PIN code
 * @return ESP_OK on success
 */
esp_err_t bt_send_pin_code(const char *pin);

/**
 * @brief Set the default PIN code
 * @param pin PIN code
 * @return ESP_OK on success
 */
esp_err_t bt_set_default_pin(const char *pin);

/**
 * @brief Get the default PIN code
 * @param pin Buffer to store the PIN code
 * @param size Size of the buffer
 * @return ESP_OK on success
 */
esp_err_t bt_get_default_pin(char *pin, size_t size);

/**
 * @brief Get the SSP passkey
 * @return SSP passkey
 */
uint32_t bt_get_ssp_passkey(void);

/**
 * @brief Check if SSP confirmation is requested
 * @return true if confirmation requested
 */
bool bt_is_ssp_confirm_requested(void);

/**
 * @brief Confirm or reject SSP pairing
 * @param confirm true to accept, false to reject
 * @return ESP_OK on success
 */
esp_err_t bt_ssp_confirm(bool confirm);

/**
 * @brief Unpair a device
 * @param addr Device address
 * @return ESP_OK on success
 */
esp_err_t bt_unpair_device(esp_bd_addr_t addr);

/**
 * @brief Unpair all devices
 * @return ESP_OK on success
 */
esp_err_t bt_unpair_all_devices(void);

/**
 * @brief Get the number of paired devices
 * @return Number of paired devices
 */
int bt_get_paired_device_count(void);

/**
 * @brief Get paired devices
 * @param devices Array to store device information
 * @param count Number of devices to return
 * @return Number of devices found
 */
int bt_get_paired_devices(bt_device_info_t *devices, int count);

/**
 * @brief Get information about a paired device
 * @param addr Device address
 * @param info Pointer to store device information
 * @return ESP_OK on success
 */
esp_err_t bt_get_paired_device_info(esp_bd_addr_t addr, bt_device_info_t *info);

/**
 * @brief Store paired devices to NVS
 * @return ESP_OK on success
 */
esp_err_t bt_store_paired_devices(void);

/**
 * @brief Load paired devices from NVS
 * @return ESP_OK on success
 */
esp_err_t bt_load_paired_devices(void);

#ifdef __cplusplus
}
#endif

#endif /* _BT_INTERFACE_H_ */
