/**
 * @file bt_interface.h
 * @brief Bluetooth interface header
 */

#ifndef BT_INTERFACE_H
#define BT_INTERFACE_H

#include "esp_err.h"
#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

// Define BT_MAX_DEVICE_NAME_LEN if not already defined
#ifndef BT_MAX_DEVICE_NAME_LEN
#define BT_MAX_DEVICE_NAME_LEN 64
#endif

// Define pairing state enum
typedef enum {
    BT_PAIRING_STATE_IDLE = 0,
    BT_PAIRING_STATE_STARTED = 1,
    BT_PAIRING_STATE_PIN_REQUESTED = 2,
    BT_PAIRING_STATE_SSP_REQUESTED = 3,
    BT_PAIRING_STATE_PAIRED = 4,
    BT_PAIRING_STATE_FAILED = 5,
    BT_PAIRING_STATE_TIMEOUT = 6
} bt_pairing_state_t;

// Define pairing method enum
typedef enum {
    BT_PAIRING_METHOD_NONE = 0,
    BT_PAIRING_METHOD_PIN = 1,
    BT_PAIRING_METHOD_SSP = 2
} bt_pairing_method_t;

// Bluetooth device information structure
typedef struct {
    esp_bd_addr_t addr;                     /*!< Bluetooth device address */
    char name[BT_MAX_DEVICE_NAME_LEN];     /*!< Bluetooth device name */
    bool supports_a2dp;                    /*!< Device supports A2DP profile */
} bt_device_info_t;

// Bluetooth stream state enum
typedef enum {
    BT_STREAM_STATE_IDLE = 0,
    BT_STREAM_STATE_STREAMING = 1,
    BT_STREAM_STATE_PAUSED = 2
} bt_stream_state_t;

// Add this enum if it doesn't exist in this file
#ifndef BT_DEVICE_TYPE_T_DEFINED
#define BT_DEVICE_TYPE_T_DEFINED
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
#endif

// Initialize Bluetooth stack
esp_err_t bt_init(void);

// Deinitialize Bluetooth stack
esp_err_t bt_deinit(void);

// Connect to a Bluetooth device by address
esp_err_t bt_connect(esp_bd_addr_t addr);

// Disconnect from the currently connected Bluetooth device
esp_err_t bt_disconnect(void);

// Check if connected to a Bluetooth device
bool bt_is_connected(void);

// Get connection information of the currently connected device
esp_err_t bt_get_connection_info(bt_device_info_t *info);

// Start device scan
esp_err_t bt_scan(void);

// Stop device scan
esp_err_t bt_scan_stop(void);

// Start Bluetooth scan (legacy)
esp_err_t bt_scan_start(void);

/**
 * Starts a filtered Bluetooth scan, looking for devices of a specific type.
 *
 * @param device_type The type of devices to look for
 * @return ESP_OK if scan started successfully, an error code otherwise
 */
esp_err_t bt_scan_start_filtered(bt_device_type_t device_type);

// Check if Bluetooth is currently scanning
bool bt_is_scanning(void);

// Get the count of discovered devices from the last scan
int bt_get_discovered_device_count(void);

// Get the list of discovered devices from the last scan
int bt_get_discovered_devices(bt_device_info_t *devices, int count, uint16_t *actual_count);

// Connect to a Bluetooth device by name
esp_err_t bt_connect_by_name(const char *name);

// Connect to a Bluetooth device with a timeout
esp_err_t bt_connect_with_timeout(esp_bd_addr_t addr, int timeout_ms);

// Start audio streaming to the connected device
esp_err_t bt_start_streaming(void);

// Stop audio streaming
esp_err_t bt_stop_streaming(void);

// Pause audio streaming
esp_err_t bt_pause_streaming(void);

// Resume audio streaming
esp_err_t bt_resume_streaming(void);

// Check if currently streaming audio
bool bt_is_streaming(void);

// Check if audio streaming is paused
bool bt_is_paused(void);

// Get the current streaming state
bt_stream_state_t bt_get_streaming_state(void);

// Check if a device supports a specific profile
bool bt_device_supports_profile(esp_bd_addr_t addr, uint16_t profile_id);

// Start pairing with a Bluetooth device
esp_err_t bt_start_pairing(esp_bd_addr_t addr);

// Check if a device is paired
bool bt_is_device_paired(esp_bd_addr_t addr);

// Get the current pairing state
bt_pairing_state_t bt_get_pairing_state(void);

// Get the current pairing method
bt_pairing_method_t bt_get_pairing_method(void);

// Send the PIN code during pairing
esp_err_t bt_send_pin_code(const char *pin);

// Set the default PIN code for pairing
esp_err_t bt_set_default_pin(const char *pin);

// Get the default PIN code for pairing
esp_err_t bt_get_default_pin(char *pin, size_t size);

// Get the SSP passkey for pairing
uint32_t bt_get_ssp_passkey(void);

// Check if SSP confirmation is requested
bool bt_is_ssp_confirm_requested(void);

// Confirm or reject SSP pairing request
esp_err_t bt_ssp_confirm(bool confirm);

// Unpair a Bluetooth device
esp_err_t bt_unpair_device(esp_bd_addr_t addr);

// Unpair all Bluetooth devices
esp_err_t bt_unpair_all_devices(void);

// Get the count of paired devices
int bt_get_paired_device_count(void);

// Get the list of paired devices
int bt_get_paired_devices(bt_device_info_t *devices, int count);

// Get detailed information of a paired device
esp_err_t bt_get_paired_device_info(esp_bd_addr_t addr, bt_device_info_t *info);

// Store paired devices to persistent storage
esp_err_t bt_store_paired_devices(void);

// Load paired devices from persistent storage
esp_err_t bt_load_paired_devices(void);

// Enable or disable auto-reconnect feature
esp_err_t bt_set_auto_reconnect(bool enable);

// Simulate a device disconnect (for testing)
esp_err_t bt_simulate_disconnect(void);

// Add these function prototypes for our helper functions
esp_err_t bt_add_paired_device(esp_bd_addr_t addr, const char *name, bool supports_a2dp);
esp_err_t bt_reset_paired_devices(void);

#endif // BT_INTERFACE_H