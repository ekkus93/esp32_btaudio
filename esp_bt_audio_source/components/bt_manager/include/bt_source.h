#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
// Add necessary ESP-IDF includes
#include "esp_a2dp_api.h"
#include "esp_gap_bt_api.h"

/* If bt_source.h exposes test helper prototypes (bt_mock_*), advertise that
 * to test-only headers so they avoid emitting conflicting static inline
 * wrappers. Multiple headers may define this; guard to avoid redefinition.
 */
#ifndef BT_MOCK_PROVIDES_PROTOTYPES
#define BT_MOCK_PROVIDES_PROTOTYPES 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Device information structure
#if !defined(BT_SOURCE_SKIP_DEVICE_STRUCT)
typedef struct {
    uint8_t addr[6];      // Bluetooth address (MAC)
    char name[64];        // Device name
    int8_t rssi;          // Signal strength
    uint32_t cod;         // Class of device
    bool paired;          // Is device paired?
} bt_device_t;
#endif

// Bluetooth connection states
typedef enum {
    BT_CONNECTION_STATE_DISCONNECTED = 0,
    BT_CONNECTION_STATE_CONNECTING = 1,
    BT_CONNECTION_STATE_CONNECTED = 2,
    BT_CONNECTION_STATE_DISCONNECTING = 3,
    BT_CONNECTION_STATE_FAILED = 4
} bt_connection_state_t;

// Connection info structure
typedef struct {
    bool connected;       // Is currently connected
    char addr[18];        // Connected device address
    char name[64];        // Connected device name
    bool streaming;       // Is audio streaming active
    // State tracking
    bt_connection_state_t state;    // Current connection state
    uint32_t connect_time;          // Time when connected (Unix timestamp)
    uint8_t retry_count;            // Number of connection retries
} bt_connection_info_t;

// Bluetooth device types
typedef enum {
    BT_DEVICE_TYPE_ALL,   // All types
    BT_DEVICE_TYPE_AUDIO, // Audio devices (speakers, headphones)
    BT_DEVICE_TYPE_PHONE, // Phones
    BT_DEVICE_TYPE_OTHER  // Other types
} bt_device_type_t;

// Bluetooth pairing states
typedef enum {
    BT_PAIRING_STATE_IDLE = 0,        // Not pairing
    BT_PAIRING_STATE_STARTED = 1,     // Pairing started
    BT_PAIRING_STATE_PIN_REQUESTED = 2, // PIN code requested
    BT_PAIRING_STATE_SSP_REQUESTED = 3, // SSP confirmation requested
    BT_PAIRING_STATE_PAIRED = 4,      // Pairing successful
    BT_PAIRING_STATE_FAILED = 5,      // Pairing failed
    BT_PAIRING_STATE_TIMEOUT = 6      // Pairing timeout
} bt_pairing_state_t;

// Bluetooth pairing methods
typedef enum {
    BT_PAIRING_METHOD_NONE = 0,       // Not pairing
    BT_PAIRING_METHOD_JUST_WORKS = 1, // Just Works
    BT_PAIRING_METHOD_PIN = 2,        // PIN code
    BT_PAIRING_METHOD_SSP = 3         // Secure Simple Pairing
} bt_pairing_method_t;

// Bluetooth streaming states - make sure there's only ONE definition
typedef enum {
    BT_STREAMING_STATE_STOPPED = 0,    // Streaming stopped 
    BT_STREAMING_STATE_STARTING = 1,   // Streaming starting
    BT_STREAMING_STATE_STREAMING = 2,  // Streaming active (playing)
    BT_STREAMING_STATE_STOPPING = 3,   // Streaming stopping
    BT_STREAMING_STATE_PAUSED = 4,     // Streaming paused
    BT_STREAMING_STATE_ERROR = 5       // Streaming error
} bt_streaming_state_t;

// Profile enum
typedef enum {
    BT_PROFILE_A2DP_SINK = 0,        // A2DP Sink
    BT_PROFILE_A2DP_SOURCE = 1,      // A2DP Source
    BT_PROFILE_HFP = 2,              // Hands-Free Profile
    BT_PROFILE_AVRCP = 3,            // Audio/Video Remote Control Profile
    BT_PROFILE_SPP = 4,              // Serial Port Profile
    BT_PROFILE_PBAP = 5              // Phone Book Access Profile
} bt_profile_t;

/**
 * @brief Information about the current streaming session
 */
typedef struct {
    bt_streaming_state_t state;     // Current streaming state
    uint32_t bytes_sent;            // DEPRECATED: use bytes_requested instead
    uint32_t packets_sent;          // Packets sent during streaming
    uint32_t packet_errors;         // Packet errors during streaming
    uint32_t stream_duration;       // in milliseconds
    bool paused;                    // Is streaming paused
    /* CODE_REVIEW5 Task 3.1: Split audio vs silence tracking */
    uint32_t bytes_requested;       // Total bytes A2DP requested
    uint32_t bytes_produced;        // Actual audio bytes from queue
    uint32_t bytes_silence;         // Zero-fill bytes (underruns)
    /* CODE_REVIEW5 Task 3.2: Underrun rate tracking */
    uint32_t underrun_count;        // Number of callbacks with underruns
    uint32_t total_callbacks;       // Total A2DP data callbacks
} bt_streaming_info_t;

// Callback type definitions
typedef void (*bt_discovery_cb_t)(bt_device_t* device, void* user_data);
typedef void (*bt_connection_callback_t)(bt_connection_info_t* info, void* user_data);
typedef void (*bt_stream_callback_t)(bool streaming, const bt_streaming_info_t* info, void* user_data);

// Interface structure for BT implementation
typedef struct {
    // Implementation-specific data
    void* priv;
} bt_interface_t;

/* ----------------- Core Bluetooth API ----------------- */

/**
 * @brief Initialize Bluetooth stack
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_init(void);

/**
 * @brief Cleanup Bluetooth stack
 * 
 * @return ESP_OK on success
 */
void bt_cleanup(void);

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
esp_err_t bt_connect_device(const char* addr);

/**
 * @brief Connect to a device by name
 * 
 * @param name Device name to connect to
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bt_connect_device_by_name(const char* name);

/**
 * @brief Disconnect from current device
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_disconnect(void);

/**
 * @brief Check if connected to a BT device
 * 
 * @return true if connected, false otherwise
 */
bool bt_is_connected(void);

/**
 * @brief Get current connection information
 * 
 * @param info Pointer to connection info structure to fill
 * @return ESP_OK on success
 */
esp_err_t bt_get_connection_info(bt_connection_info_t* info);

/**
 * @brief Get current connection state
 * 
 * @return Current connection state
 */
bt_connection_state_t bt_get_connection_state_detailed(void);

/**
 * @brief Get current connection state as integer (simpler version)
 * 
 * @return 1 if connected, 0 if not connected
 */
int bt_get_connection_state(void);

/* ----------------- A2DP Streaming API ----------------- */

/**
 * @brief Start audio streaming
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_a2dp_start_streaming(void);

/**
 * @brief Stop audio streaming
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_a2dp_stop_streaming(void);

/**
 * @brief Pause audio streaming
 * 
 * @return ESP_OK on success, ESP_FAIL if not streaming
 */
esp_err_t bt_a2dp_pause_streaming(void);

/**
 * @brief Resume previously paused audio streaming
 * 
 * @return ESP_OK on success, ESP_FAIL if not paused
 */
esp_err_t bt_a2dp_resume_streaming(void);

/**
 * @brief Check if streaming is active
 * 
 * @return true if streaming, false otherwise
 */
bool bt_a2dp_is_streaming(void);

/**
 * @brief Check if A2DP is connected
 * 
 * @return true if connected, false otherwise
 */
bool bt_a2dp_is_connected(void);

/* ----------------- Device Management API ----------------- */

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
 * @brief Check if streaming is paused
 * 
 * @return true if paused, false otherwise
 */
bool bt_is_paused(void);

/**
 * @brief Get current streaming state
 * 
 * @return Current streaming state
 */
bt_streaming_state_t bt_get_streaming_state(void);

/**
 * @brief Get detailed streaming info
 * 
 * @param info Pointer to streaming info structure to fill
 * @return ESP_OK on success
 */
esp_err_t bt_get_streaming_info(bt_streaming_info_t* info);

/* BT Manager status - returned by bt_manager_get_status() (CODE_REVIEW8 P2) */
typedef struct {
    bool initialized;       /* BT manager initialized */
    bool connected;         /* Device connected */
    bool audio_playing;     /* Audio streaming active */
    bool scanning;          /* Device scan in progress */
    char connected_mac[18]; /* Connected device MAC (empty if not connected) */
    char connected_name[32];/* Connected device name (empty if not connected) */
} bt_manager_status_t;

/**
 * @brief Get BT manager status from ANY task (thread-safe)
 * 
 * This function provides thread-safe access to BT manager state by posting
 * a request to BtAppTask and waiting for a consistent snapshot. Safe to call
 * from any task context.
 * 
 * @param[out] status Pointer to status structure (filled on success)
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if BtAppTask doesn't respond,
 *         ESP_ERR_INVALID_ARG if status is NULL, ESP_FAIL on queue/semaphore error
 * @note Blocks up to 100ms waiting for response. Do not call from ISR.
 */
esp_err_t bt_manager_get_status(bt_manager_status_t *status);

/* ----------------- Pairing API ----------------- */

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
 * @brief Get current SSP passkey (for tests)
 *
 * @param passkey Buffer to receive passkey string
 * @param size Size of buffer
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no passkey available
 */
esp_err_t bt_get_ssp_passkey(char* passkey, size_t size);

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

#if CONFIG_BT_MOCK_TESTING
/* Test-only reconnect sequencing hooks (device/host). */
void bt_conn_test_set_reconnect_results(const esp_err_t *results, size_t len);
void bt_conn_test_set_reconnect_delay_ms(uint32_t delay_ms);
void bt_conn_test_reset_state(void);
#endif

/* ----------------- TESTING FUNCTIONS (NOT FOR PRODUCTION) ----------------- */
/**
 * @brief Add a test device (for testing purposes only)
 * @warning ONLY FOR USE IN TESTING
 */
void bt_mock_add_test_device(const char* addr_str, const char* name, bt_device_type_t type);

/**
 * @brief Simulate PIN pairing failure (for testing purposes only)
 * @warning ONLY FOR USE IN TESTING
 */
void bt_mock_simulate_pin_failure(void);

/**
 * @brief Simulate pairing timeout (for testing purposes only)
 * @warning ONLY FOR USE IN TESTING
 */
void bt_mock_simulate_pairing_timeout(void);

/**
 * @brief Set whether SSP is supported (for testing purposes only)
 * @warning ONLY FOR USE IN TESTING
 */
void bt_mock_set_ssp_supported(bool supported);

/**
 * @brief Reset Bluetooth stack state (for testing purposes only)
 * @warning ONLY FOR USE IN TESTING
 */
void bt_mock_reset(void); // Changed from bt_reset_for_test to bt_mock_reset

// Connection manager functions with correct ESP-IDF types
void bt_connection_manager_init(esp_a2d_cb_t conn_cb, esp_a2d_source_data_cb_t audio_cb);

#ifdef __cplusplus
}
#endif
