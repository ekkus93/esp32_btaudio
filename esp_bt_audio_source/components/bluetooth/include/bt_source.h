#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Connection states for the BT connection
 */
typedef enum {
    BT_STATE_UNKNOWN = 0,
    BT_STATE_DISCONNECTED,
    BT_STATE_CONNECTING,
    BT_STATE_CONNECTED,
    BT_STATE_DISCONNECTING,
    BT_STATE_ERROR
} bt_connection_state_t;

/**
 * @brief Streaming states for audio
 */
typedef enum {
    BT_STREAM_STATE_UNKNOWN = 0,
    BT_STREAM_STATE_IDLE,      // <-- Add this line
    BT_STREAM_STATE_STOPPED,
    BT_STREAM_STATE_STREAMING,
    BT_STREAM_STATE_PAUSED,
    BT_STREAM_STATE_PLAYING,
    BT_STREAM_STATE_ERROR
} bt_streaming_state_t;

/**
 * Bluetooth connection information structure
 */
typedef struct {
    bool connected;
    char remote_addr[18];
    char remote_name[64];
    uint32_t profile;
    bool supports_a2dp;
} bt_connection_info_t;

/**
 * @brief Enum defining the different types of Bluetooth devices
 */
typedef enum {
    BT_DEVICE_TYPE_UNKNOWN = 0,
    BT_DEVICE_TYPE_AUDIO = 1,
    BT_DEVICE_TYPE_PHONE = 2,
    BT_DEVICE_TYPE_COMPUTER = 3,
    BT_DEVICE_TYPE_HEADSET = 4,
    BT_DEVICE_TYPE_SPEAKER = 5
} bt_device_type_t;

/**
 * @brief Enum defining the Bluetooth pairing states
 */
typedef enum {
    BT_PAIRING_STATE_IDLE = 0,
    BT_PAIRING_STATE_STARTED = 1,
    BT_PAIRING_STATE_PIN_REQUESTED = 2,
    BT_PAIRING_STATE_SSP_REQUESTED = 3,
    BT_PAIRING_STATE_PAIRED = 4,
    BT_PAIRING_STATE_FAILED = 5,
    BT_PAIRING_STATE_TIMEOUT = 6
} bt_pairing_state_t;

/**
 * @brief Enum defining the Bluetooth pairing methods
 */
typedef enum {
    BT_PAIRING_METHOD_NONE = 0,
    BT_PAIRING_METHOD_PIN = 1,
    BT_PAIRING_METHOD_SSP = 2
} bt_pairing_method_t;

/**
 * BT profile type enum
 */
typedef enum {
    BT_PROFILE_NONE = 0,
    BT_PROFILE_A2DP_SINK = (1 << 0),
    BT_PROFILE_A2DP_SOURCE = (1 << 1),
    BT_PROFILE_HFP = (1 << 2),
    BT_PROFILE_SPP = (1 << 3)
} bt_profile_t;

/**
 * @brief BT device structure
 */
typedef struct {
    uint8_t addr[6];           // Device address
    char name[32];             // Device name
    int8_t rssi;               // Signal strength
    bool paired;               // Is device paired
    uint32_t cod;              // Class of Device
} bt_device_t;

/**
 * Bluetooth device information structure
 */
typedef struct {
    uint8_t addr[6];
    char name[64];
    bool supports_a2dp;
} bt_device_info_t;

/**
 * @brief Discovery callback function type
 */
typedef void (*bt_discovery_cb_t)(bt_device_t *device, void *user_data);

/**
 * @brief Connection state change callback type
 */
typedef void (*bt_connection_callback_t)(bool connected, bt_device_t* device, esp_err_t status, void* user_data);

/**
 * @brief Initialize Bluetooth stack
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_init(void);

/**
 * @brief Start BT scanning
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_scan_start(void);

/**
 * @brief Start filtered scan for specific device type
 * 
 * @param device_type Device type to filter for
 * @return ESP_OK on success
 */
esp_err_t bt_scan_start_filtered(bt_device_type_t device_type);

/**
 * @brief Stop scanning
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_scan_stop(void);

/**
 * @brief Connect to a device
 * 
 * @param addr MAC address string in format "XX:XX:XX:XX:XX:XX"
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_connect(const char* addr);

/**
 * @brief Connect to a device by name
 * 
 * @param name Device name to connect to
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_connect_by_name(const char* name);

/**
 * @brief Connect with timeout
 * 
 * @param addr MAC address string in format "XX:XX:XX:XX:XX:XX"
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_connect_with_timeout(const char* addr, uint32_t timeout_ms);

/**
 * @brief Check if connected to a BT device
 * 
 * @return true if connected, false otherwise
 */
bool bt_is_connected(void);

/**
 * @brief Disconnect from current device
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_disconnect(void);

/**
 * @brief Start audio streaming
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_start_streaming(void);

/**
 * @brief Stop audio streaming
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_stop_streaming(void);

/**
 * @brief Check if streaming is active
 * 
 * @return true if streaming, false otherwise
 */
bool bt_is_streaming(void);

/**
 * @brief Register device discovery callback
 * 
 * @param callback Function to call when device is discovered
 * @param user_data User data to pass to callback
 * @return ESP_OK on success
 */
esp_err_t bt_register_discovery_callback(bt_discovery_cb_t callback, void *user_data);

/**
 * @brief Get discovered device count
 * 
 * @return Number of discovered devices
 */
uint16_t bt_get_discovered_device_count(void);

/**
 * @brief Get discovered devices
 * 
 * @param devices Array to store devices (must be allocated by caller)
 * @param max_count Maximum number of devices to store
 * @param device_count Actual number of devices stored
 * @return ESP_OK on success
 */
esp_err_t bt_get_discovered_devices(bt_device_t *devices, uint16_t count, uint16_t *actual_count);

/**
 * Unpair all devices
 * 
 * @return ESP_OK if successful
 */
esp_err_t bt_unpair_all_devices(void);

/**
 * Get count of paired devices
 * 
 * @return Number of paired devices
 */
uint16_t bt_get_paired_device_count(void);

/**
 * @brief Add paired device
 * 
 * @param device Device to add
 * @return ESP_OK on success
 */
esp_err_t bt_add_paired_device(bt_device_t* device);

/**
 * @brief Remove paired device
 * 
 * @param device Device to remove
 * @return ESP_OK on success
 */
esp_err_t bt_remove_paired_device(bt_device_t* device);

/**
 * @brief Register callback for connection state changes
 * 
 * @param callback Function to call when connection state changes
 * @param user_data User data to pass to callback
 * @return ESP_OK on success
 */
esp_err_t bt_register_connection_callback(bt_connection_callback_t callback, void* user_data);

/**
 * @brief Get current connection information
 * 
 * @param info Pointer to connection info structure to fill
 * @return ESP_OK on success
 */
esp_err_t bt_get_connection_info(bt_connection_info_t* info);

/**
 * @brief Enable or disable auto-reconnection
 * 
 * @param enable True to enable auto-reconnect, false to disable
 * @return ESP_OK on success
 */
esp_err_t bt_set_auto_reconnect(bool enable);

/**
 * @brief Simulate disconnection (for testing only)
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_simulate_disconnect(void);

/**
 * @brief Check if device supports a profile
 * 
 * @param device Device to check
 * @param profile Profile to check for
 * @return true if supported, false otherwise
 */
bool bt_device_supports_profile(const bt_device_t* device, bt_profile_t profile);

/**
 * @brief Start BT scanning with timeout
 * 
 * @param duration_s Duration in seconds
 * @return ESP_OK on success
 */
esp_err_t bt_scan(uint32_t duration_s);

/**
 * @brief Check if BT is currently scanning
 * 
 * @return true if scanning, false otherwise
 */
bool bt_is_scanning(void);

/**
 * @brief Pause streaming audio
 * 
 * @return ESP_OK on success, ESP_FAIL if not streaming
 */
esp_err_t bt_pause_streaming(void);

/**
 * @brief Resume previously paused audio streaming
 * 
 * @return ESP_OK on success, ESP_FAIL if not paused
 */
esp_err_t bt_resume_streaming(void);

/**
 * @brief Check if streaming is paused
 * 
 * @return true if paused, false otherwise
 */
bool bt_is_paused(void);

/**
 * @brief Get current streaming state
 * 
 * @return Current streaming state (STOPPED, PLAYING, PAUSED)
 */
bt_streaming_state_t bt_get_streaming_state(void);

/**
 * @brief Information about the current streaming session
 */
typedef struct {
    bt_streaming_state_t state;
    uint32_t bytes_sent;
    uint32_t packets_sent;
    uint32_t packet_errors;
    uint32_t stream_duration; // in milliseconds
    bool paused;
} bt_streaming_info_t;

/**
 * @brief Callback type for streaming state changes
 * 
 * @param streaming True if streaming is active
 * @param info Pointer to current streaming info
 * @param user_data User data pointer
 */
typedef void (*bt_stream_callback_t)(bool streaming, const bt_streaming_info_t* info, void* user_data);

/**
 * @brief Start Bluetooth pairing with a device
 *
 * @param addr MAC address of the device
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bt_start_pairing(const char* addr);

/**
 * @brief Send PIN code for pairing
 *
 * @param pin PIN code as string
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bt_send_pin_code(const char* pin);

/**
 * @brief Get current pairing state
 *
 * @return bt_pairing_state_t Current pairing state
 */
bt_pairing_state_t bt_get_pairing_state(void);

/**
 * @brief Check if a device is paired
 *
 * @param addr MAC address of the device
 * @return bool True if paired
 */
bool bt_is_device_paired(const char* addr);

/**
 * @brief Set default PIN code
 *
 * @param pin PIN code to use as default
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bt_set_default_pin(const char* pin);

/**
 * @brief Get default PIN code
 *
 * @param pin Buffer to store the PIN
 * @param size Size of the buffer
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bt_get_default_pin(char* pin, size_t size);

/**
 * Unpair a specific device
 * 
 * @param addr Device address
 * @return ESP_OK if successful, ESP_ERR_NOT_FOUND if device not found
 */
esp_err_t bt_unpair_device(const char* addr);

/**
 * Store paired devices to persistent storage
 * 
 * @return ESP_OK if successful
 */
esp_err_t bt_store_paired_devices(void);

/**
 * Load paired devices from persistent storage
 * 
 * @return ESP_OK if successful
 */
esp_err_t bt_load_paired_devices(void);

/**
 * Get detailed connection info for a specific paired device
 * 
 * @param addr Device address
 * @param info Pointer to connection info structure
 * @return ESP_OK if successful, ESP_ERR_NOT_FOUND if device not found
 */
esp_err_t bt_get_paired_device_info(const char* addr, bt_connection_info_t* info);

/**
 * Add a test device (for testing purposes only)
 * 
 * @param addr_str Device address string
 * @param name Device name
 * @param type Device type
 */
void bt_mock_add_test_device(const char* addr_str, const char* name, bt_device_type_t type);

/* Mock functions for testing - these should only be defined in the test code */
void bt_mock_simulate_pin_failure(void);
void bt_mock_simulate_pairing_timeout(void);

/* SSP Pairing Functions */

/**
 * Respond to a SSP confirmation request
 * 
 * @param confirm True to accept the pairing, false to reject
 * @return ESP_OK if the response was sent successfully
 */
esp_err_t bt_ssp_confirm(bool confirm);

/**
 * Get the current SSP passkey/code displayed by remote device
 *
 * @param passkey Buffer to store the passkey (should be at least 7 bytes)
 * @param size Size of the passkey buffer
 * @return ESP_OK if successful, ESP_ERR_NOT_FOUND if no SSP request is active
 */
esp_err_t bt_get_ssp_passkey(char* passkey, size_t size);

/**
 * Check if a SSP confirmation request is active
 *
 * @return true if SSP confirmation is requested, false otherwise
 */
bool bt_is_ssp_confirm_requested(void);

/**
 * Get the current pairing method being used
 *
 * @return Current pairing method
 */
bt_pairing_method_t bt_get_pairing_method(void);

/**
 * Set whether SSP is supported by the mock
 * For testing purposes only
 * 
 * @param supported Whether SSP is supported
 */
void bt_mock_set_ssp_supported(bool supported);

/**
 * Get a list of paired devices
 *
 * @param devices Array to populate with paired device information
 * @param max_devices Maximum number of devices to retrieve
 * @return Number of devices retrieved
 */
int bt_get_paired_devices(bt_device_t* devices, int max_devices);

/**
 * @brief Check if there are any devices that match the given filter
 * 
 * @param device_type Type of device to filter
 * @return true if there are matching devices, false otherwise
 */
bool bt_filter_has_matches(int device_type);

/**
 * Reset Bluetooth stack state for testing purposes
 * This function should only be used in test environments
 */
void bt_reset_for_test(void);

#ifdef __cplusplus
}
#endif
