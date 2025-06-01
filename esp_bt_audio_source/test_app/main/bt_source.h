#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bluetooth device types for filtering
 */
typedef enum {
    BT_DEVICE_TYPE_UNKNOWN = 0,
    BT_DEVICE_TYPE_CLASSIC = 1,
    BT_DEVICE_TYPE_LE = 2, 
    BT_DEVICE_TYPE_DUAL = 3,
    BT_DEVICE_TYPE_A2DP_SINK = 5,  // Added for test case
} bt_device_type_t;

/**
 * @brief Bluetooth profile masks
 */
typedef enum {
    BT_PROFILE_NONE = 0x0,
    BT_PROFILE_A2DP_SINK = 0x1,
    BT_PROFILE_A2DP_SOURCE = 0x2,
    BT_PROFILE_HFP = 0x4,
    BT_PROFILE_AVRCP = 0x8,
} bt_profile_t;

/**
 * @brief Bluetooth device information structure
 */
typedef struct {
    char name[32];         // Device name
    char addr[18];         // Device address (xx:xx:xx:xx:xx:xx format)
    int rssi;              // Signal strength
    uint32_t profiles;     // Supported profiles (bitmask of bt_profile_t values)
    bt_device_type_t type; // Device type
} bt_device_t;

/**
 * @brief Connection state enum
 */
typedef enum {
    BT_CONNECTION_STATE_DISCONNECTED = 0,
    BT_CONNECTION_STATE_CONNECTING,
    BT_CONNECTION_STATE_CONNECTED,
    BT_CONNECTION_STATE_DISCONNECTING,
    BT_CONNECTION_STATE_FAILED
} bt_connection_state_t;

/**
 * @brief Streaming state enum
 */
typedef enum {
    BT_STREAMING_STATE_STOPPED = 0,
    BT_STREAMING_STATE_STARTING,
    BT_STREAMING_STATE_STREAMING,
    BT_STREAMING_STATE_PAUSED,
    BT_STREAMING_STATE_STOPPING,
    BT_STREAMING_STATE_ERROR
} bt_streaming_state_t;

/**
 * @brief Bluetooth connection information structure
 */
typedef struct {
    bool connected;           // Whether device is connected
    char remote_addr[18];     // Remote device address
    char remote_name[32];     // Remote device name
    int connection_quality;   // Connection quality (0-100)
    bt_connection_state_t state; // Detailed connection state
    uint32_t connect_time;    // Timestamp of when connection was established
    uint16_t retry_count;     // Connection retry count
} bt_connection_info_t;

/**
 * @brief Streaming info structure
 */
typedef struct {
    bt_streaming_state_t state;  // Current streaming state
    uint32_t bytes_sent;         // Bytes sent in current session
    uint32_t packets_sent;       // Packets sent in current session
    uint32_t packet_errors;      // Error packets in current session
    uint32_t stream_duration;    // Duration of current stream in milliseconds
    bool paused;                 // Whether stream is paused
} bt_streaming_info_t;

/**
 * @brief Connection callback function type
 */
typedef void (*bt_connection_callback_t)(bool connected, esp_err_t status, void* user_data);

/**
 * @brief Streaming callback function type
 */
typedef void (*bt_stream_callback_t)(bool streaming, esp_err_t status, void* user_data);

/**
 * @brief Initialize Bluetooth module
 * @return ESP_OK on success
 */
esp_err_t bt_init(void);

/**
 * @brief Deinitialize Bluetooth module
 * @return ESP_OK on success
 */
esp_err_t bt_deinit(void);

/**
 * @brief Check if Bluetooth is initialized
 * @return true if initialized
 */
bool bt_is_initialized(void);

/**
 * @brief Register callback for connection events
 * @param callback Function to call when connection status changes
 * @param user_data User data to pass to the callback
 * @return ESP_OK on success
 */
esp_err_t bt_register_connection_callback(bt_connection_callback_t callback, void* user_data);

/**
 * @brief Register callback for streaming events
 * @param callback Function to call when streaming status changes
 * @param user_data User data to pass to the callback
 * @return ESP_OK on success
 */
esp_err_t bt_register_streaming_callback(bt_stream_callback_t callback, void* user_data);

/**
 * @brief Start scanning for Bluetooth devices
 * @return ESP_OK on success
 */
esp_err_t bt_scan_start(void);

/**
 * @brief Stop scanning for Bluetooth devices
 * @return ESP_OK on success
 */
esp_err_t bt_scan_stop(void);

/**
 * @brief Scan for specified duration
 * @param duration_s Duration in seconds
 * @return ESP_OK on success
 */
esp_err_t bt_scan(uint32_t duration_s);

/**
 * @brief Start scanning with device type filter
 * @param device_type Device type to filter for
 * @return ESP_OK on success
 */
esp_err_t bt_scan_start_filtered(bt_device_type_t device_type);

/**
 * @brief Check if filter has matching devices
 * @param device_type Device type to check
 * @return true if there are matches
 */
bool bt_filter_has_matches(bt_device_type_t device_type);

/**
 * @brief Get number of discovered devices
 * @return Count of discovered devices
 */
uint16_t bt_get_discovered_device_count(void);

/**
 * @brief Get discovered devices list
 * @param devices Buffer to store device information
 * @param max_count Maximum number of devices to retrieve
 * @param count Actual number of devices retrieved
 * @return ESP_OK on success
 */
esp_err_t bt_get_discovered_devices(bt_device_t* devices, uint16_t max_count, uint16_t* count);

/**
 * @brief Connect to device by address
 * @param addr Address of device to connect to
 * @return ESP_OK on success
 */
esp_err_t bt_connect(const char* addr);

/**
 * @brief Connect to device by name
 * @param name Name of device to connect to
 * @return ESP_OK on success
 */
esp_err_t bt_connect_by_name(const char* name);

/**
 * @brief Connect to device with timeout
 * @param addr Address of device to connect to
 * @param timeout_ms Connection timeout in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t bt_connect_with_timeout(const char* addr, uint32_t timeout_ms);

/**
 * @brief Disconnect from current device
 * @return ESP_OK on success
 */
esp_err_t bt_disconnect(void);

/**
 * @brief Check if connected to a device
 * @return true if connected
 */
bool bt_is_connected(void);

/**
 * @brief Get connection info
 * @param info Pointer to store connection info
 * @return ESP_OK on success
 */
esp_err_t bt_get_connection_info(bt_connection_info_t* info);

/**
 * @brief Get current streaming information
 * 
 * @param info Pointer to store streaming information
 * @return ESP_OK on success, ESP_FAIL if not streaming or error
 */
esp_err_t bt_get_streaming_info(bt_streaming_info_t* info);

/**
 * @brief Start audio streaming
 * 
 * @return ESP_OK on success, ESP_FAIL if not connected or error
 */
esp_err_t bt_streaming_start(void);

/**
 * @brief Stop audio streaming
 * 
 * @return ESP_OK on success, ESP_FAIL if not streaming or error
 */
esp_err_t bt_streaming_stop(void);

/**
 * @brief Pause audio streaming
 * 
 * @return ESP_OK on success, ESP_FAIL if not streaming or error
 */
esp_err_t bt_streaming_pause(void);

/**
 * @brief Resume audio streaming
 * 
 * @return ESP_OK on success, ESP_FAIL if not paused or error
 */
esp_err_t bt_streaming_resume(void);

/**
 * @brief Get connection state 
 * @return 1 if connected, 0 otherwise
 */
int bt_get_connection_state(void);

/**
 * @brief Get streaming state 
 * @return 1 if streaming, 0 otherwise
 */
int bt_get_streaming_state(void);

/**
 * @brief Get paired devices
 * @return Pointer to paired devices array
 */
void* bt_get_paired_devices(void);

/**
 * @brief Get paired device count
 * @return Number of paired devices
 */
uint16_t bt_get_paired_device_count(void);

/**
 * @brief Simulate a connection drop
 * @return ESP_OK on success
 */
esp_err_t bt_simulate_disconnect(void);

#ifdef __cplusplus
}
#endif
