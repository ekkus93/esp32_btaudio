/**
 * Public API for ESP32 Bluetooth Audio Source
 */
#ifndef BT_SOURCE_H
#define BT_SOURCE_H

#include "esp_err.h"
#include "bt_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Bluetooth
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_init(void);

/**
 * @brief Deinitialize Bluetooth
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_deinit(void);

/**
 * @brief Reset BT state for test
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_reset_for_test(void);

/**
 * @brief Start BT scan
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_scan_start(void);

/**
 * @brief Start BT scan with filter
 * @param device_type Device type filter
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_scan_start_filtered(bt_device_type_t device_type);

/**
 * @brief Stop BT scan
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_scan_stop(void);

/**
 * @brief Start BT scan with timeout
 * @param timeout_seconds Timeout in seconds
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_scan(uint32_t timeout_seconds);

/**
 * @brief Check if currently scanning
 * @return true if scanning, false otherwise
 */
bool bt_is_scanning(void);

/**
 * @brief Get count of discovered devices
 * @return Count of discovered devices
 */
uint16_t bt_get_discovered_device_count(void);

/**
 * @brief Get discovered devices
 * @param devices Array to store discovered devices
 * @param count Max number of devices to retrieve
 * @param actual_count Actual number retrieved
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_get_discovered_devices(bt_device_t* devices, uint16_t count, uint16_t *actual_count);

/**
 * @brief Connect to device
 * @param addr Device address 
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_connect(const char* addr);

/**
 * @brief Connect to device by name
 * @param name Device name
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_connect_by_name(const char* name);

/**
 * @brief Connect to device with timeout
 * @param addr Device address
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_connect_with_timeout(const char* addr, uint32_t timeout_ms);

/**
 * @brief Disconnect from current device
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_disconnect(void);

/**
 * @brief Check if connected
 * @return true if connected, false otherwise
 */
bool bt_is_connected(void);

/**
 * @brief Get connection info
 * @param info Connection info structure to fill
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_get_connection_info(bt_connection_info_t* info);

/**
 * @brief Enable/disable auto reconnect
 * @param enable true to enable, false to disable
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_set_auto_reconnect(bool enable);

/**
 * @brief Start streaming audio
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_start_streaming(void);

/**
 * @brief Stop streaming audio
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_stop_streaming(void);

/**
 * @brief Check if streaming
 * @return true if streaming, false otherwise
 */
bool bt_is_streaming(void);

/**
 * @brief Pause streaming
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_pause_streaming(void);

/**
 * @brief Resume streaming
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_resume_streaming(void);

/**
 * @brief Check if paused
 * @return true if paused, false otherwise
 */
bool bt_is_paused(void);

/**
 * @brief Get streaming state
 * @return Current streaming state
 */
bt_streaming_state_t bt_get_streaming_state(void);

/**
 * @brief Start pairing with device
 * @param addr Device address
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_start_pairing(const char* addr);

/**
 * @brief Check if device is paired
 * @param addr Device address
 * @return true if paired, false otherwise
 */
bool bt_is_device_paired(const char* addr);

/**
 * @brief Send PIN code
 * @param pin PIN code
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_send_pin_code(const char* pin);

/**
 * @brief Set default PIN code
 * @param pin PIN code
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_set_default_pin(const char* pin);

/**
 * @brief Get default PIN code
 * @param pin Buffer to store PIN code
 * @param size Buffer size
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_get_default_pin(char* pin, size_t size);

/**
 * @brief Confirm SSP pairing
 * @param confirm true to confirm, false to reject
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_ssp_confirm(bool confirm);

/**
 * @brief Unpair device
 * @param addr Device address
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_unpair_device(const char* addr);

/**
 * @brief Unpair all devices
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_unpair_all_devices(void);

/**
 * @brief Get paired device count
 * @return Number of paired devices
 */
uint16_t bt_get_paired_device_count(void);

/**
 * @brief Get paired devices
 * @param devices Array to store devices
 * @param max_devices Maximum number of devices to retrieve
 * @return Number of devices retrieved
 */
int bt_get_paired_devices(bt_device_t* devices, int max_devices);

/**
 * @brief Store paired devices
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_store_paired_devices(void);

/**
 * @brief Load paired devices
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_load_paired_devices(void);

/**
 * @brief Add paired device
 * @param device Device to add
 * @return ESP_OK on success, or an error code
 */
esp_err_t bt_add_paired_device(bt_device_t* device);

#ifdef __cplusplus
}
#endif

#endif /* BT_SOURCE_H */
