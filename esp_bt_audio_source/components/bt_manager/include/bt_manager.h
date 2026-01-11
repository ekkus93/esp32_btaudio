#ifndef BT_MANAGER_H
#define BT_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "bt_api.h"

#if defined(ESP_PLATFORM)
#include "esp_bt.h"
#include "esp_a2dp_api.h"
#elif defined(__has_include)
#if __has_include("esp_bt.h")
#include "esp_bt.h"
#else
typedef uint8_t esp_bd_addr_t[6];
#endif
#if __has_include("esp_a2dp_api.h")
#include "esp_a2dp_api.h"
#endif
#else
#include "esp_bt.h"
#endif

#if defined(UNIT_TEST) && !defined(ESP_PLATFORM) && (!defined(__has_include) || !__has_include("esp_a2dp_api.h")) && !defined(ESP_A2D_HOST_STRUCT_DEFINED)
typedef enum {
    ESP_A2D_CONNECTION_STATE_DISCONNECTED = 0,
    ESP_A2D_CONNECTION_STATE_CONNECTING,
    ESP_A2D_CONNECTION_STATE_CONNECTED,
    ESP_A2D_CONNECTION_STATE_DISCONNECTING,
} esp_a2d_connection_state_t;

typedef enum {
    ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND = 0,
    ESP_A2D_AUDIO_STATE_STOPPED,
    ESP_A2D_AUDIO_STATE_STARTED,
} esp_a2d_audio_state_t;

typedef enum {
    ESP_A2D_CONNECTION_STATE_EVT = 0,
    ESP_A2D_AUDIO_STATE_EVT,
} esp_a2d_cb_event_t;

typedef struct {
    esp_a2d_connection_state_t state;
    esp_bd_addr_t remote_bda;
} esp_a2d_conn_stat_t;

typedef struct {
    esp_a2d_audio_state_t state;
    esp_bd_addr_t remote_bda;
} esp_a2d_audio_stat_t;

typedef union {
    esp_a2d_conn_stat_t conn_stat;
    esp_a2d_audio_stat_t audio_stat;
} esp_a2d_cb_param_t;
#endif

/**
 * Bluetooth Manager - Handles A2DP source functionality
 */

// Status codes
// Legacy bt_status_t replaced by canonical bt_err_t (esp_err_t). Use
// bt_err_t for new callers. Old enum kept for backwards compatibility where
// necessary, but public APIs below will return bt_err_t.
typedef enum {
    BT_STATUS_SUCCESS = 0,
    BT_STATUS_ERROR_INVALID_PARAM,
    BT_STATUS_ERROR_INIT_FAILED,
    BT_STATUS_ERROR_SCAN_FAILED,
    BT_STATUS_ERROR_CONNECT_FAILED,
    BT_STATUS_ERROR_NOT_INITIALIZED,
    BT_STATUS_ERROR_ALREADY_CONNECTED
} bt_status_t; // kept for compatibility, do not use for new APIs

// Device info structure
typedef struct {
    char mac[18];
    char name[32];
    int rssi;
} bt_device_t;

// Device list structure
typedef struct {
    bt_device_t devices[20];  // Maximum 20 devices
    int count;
} bt_device_list_t;

// Pending pairing request details exposed to higher layers
typedef struct {
    bool pin_request_pending;      /**< Legacy PIN request is awaiting a reply */
    bool ssp_confirm_pending;      /**< SSP numeric comparison awaiting confirm */
    char mac[18];                  /**< MAC address associated with the request */
    uint32_t passkey;              /**< SSP passkey (valid only when ssp_confirm_pending) */
} bt_pairing_request_info_t;

// Callback function types
typedef void (*bt_connected_cb)(const char* mac, const char* name);
typedef void (*bt_disconnected_cb)(const char* mac);

// Initialization parameters
typedef struct {
    const char* device_name;
    bt_connected_cb connected_cb;
    bt_disconnected_cb disconnected_cb;
} bt_manager_init_t;

/**
 * Initialize Bluetooth Manager
 *
 * @param config Initialization parameters
 * @return ESP_OK (bt_err_t) if successful
 */
bt_err_t bt_manager_init(const bt_manager_init_t* config);

/**
 * Deinitialize Bluetooth Manager
 *
 * @return ESP_OK (bt_err_t) if successful
 */
bt_err_t bt_manager_deinit(void);

/**
 * Start device scanning
 *
 * @return ESP_OK (bt_err_t) if successful
 */
bt_err_t bt_start_scan(void);

/**
 * Stop device scanning
 *
 * @return ESP_OK (bt_err_t) if successful
 */
bt_err_t bt_stop_scan(void);

/**
 * Connect to a device
 *
 * @param mac MAC address of the device
 * @return ESP_OK (bt_err_t) if successful
 */
bt_err_t bt_connect(const char* mac);

/**
 * Connect to a device by name
 *
 * @param name Name of the device
 * @return ESP_OK (bt_err_t) if successful
 */
bt_err_t bt_connect_by_name(const char* name);

/**
 * Disconnect from the current device
 *
 * @return ESP_OK (bt_err_t) if successful
 */
bt_err_t bt_disconnect(void);

/**
 * Get the list of discovered devices
 *
 * @return Pointer to device list (NULL if none)
 */
bt_device_list_t* bt_get_device_list(void);

/**
 * Get the list of paired devices
 *
 * @return Pointer to device list (NULL if none)
 */
bt_device_list_t* bt_get_paired_devices(void);

/**
 * Start audio streaming
 *
 * @return ESP_OK (bt_err_t) if successful
 */
bt_err_t bt_start_audio(void);

/**
 * Stop audio streaming
 *
 * @return ESP_OK (bt_err_t) if successful
 */
bt_err_t bt_stop_audio(void);

/**
 * @brief Enable or disable automatic A2DP start after connect/first audio.
 * When disabled, streaming must be started manually via the START command.
 */
void bt_manager_set_autostart_enabled(bool enable);

/**
 * @brief Query whether automatic A2DP start is enabled.
 */
bool bt_manager_is_autostart_enabled(void);

/**
 * Set volume level
 *
 * @param volume Volume level (0-100)
 * @return ESP_OK (bt_err_t) if successful
 */
bt_err_t bt_set_volume(int volume);

/**
 * Pair with a device
 *
 * @param mac MAC address of the device
 * @return ESP_OK (bt_err_t) if successful
 */
bt_err_t bt_pair(const char* mac);

/**
 * Unpair a device
 *
 * @param mac MAC address of the device
 * @return ESP_OK (bt_err_t) if successful
 */
bt_err_t bt_unpair(const char* mac);

/**
 * @brief Check whether the manager reports an active connection
 *
 * @return 1 if connected, 0 otherwise
 */
int bt_manager_is_connected(void);

/**
 * Wrapper used by the command interface and unit tests to start a pairing
 * operation in a test-observable way. Returns ESP_OK on success.
 */
bt_err_t bt_manager_start_pair(const char* mac);

/**
 * Unpair all devices
 *
 * @return ESP_OK (bt_err_t) if successful
 */
bt_err_t bt_unpair_all(void);

/**
 * Set PIN code for pairing
 *
 * @param pin PIN code
 * @return ESP_OK (bt_err_t) if successful
 */
bt_err_t bt_set_pin(const char* pin);

/**
 * Retrieve the current pending pairing request information (if any).
 *
 * @param info Output structure populated with pending state when available.
 * @return true when a pairing request is pending and @p info was populated.
 */
bool bt_pairing_get_pending_request(bt_pairing_request_info_t* info);

/* Legacy int-returning helpers used by command_interface and some host tests. */
int bt_manager_start_scan(void);
int bt_manager_connect(const char* mac);
int bt_manager_disconnect(void);
int bt_manager_start_audio(void);
int bt_manager_stop_audio(void);
int bt_manager_set_name(const char* name);

/**
 * Reply to an SSP numeric comparison confirmation request.
 *
 * @param mac Optional MAC address to confirm; if NULL the most recent pending
 *            request (see bt_pairing_get_pending_request) is used.
 * @param accept true to accept the pairing, false to reject it.
 * @return ESP_OK on success or an esp_err_t value on failure.
 */
bt_err_t bt_pairing_confirm(const char* mac, bool accept);

/**
 * Provide a PIN code for a legacy pairing request.
 *
 * @param mac Optional MAC address; if NULL the most recent pending request is used.
 * @param pin ASCII PIN code to reply with.
 * @return ESP_OK on success or an esp_err_t value on failure.
 */
bt_err_t bt_pairing_submit_pin(const char* mac, const char* pin);

// Functions for testing only - not for production use
#ifdef UNIT_TEST
void bt_manager_mock_device_found(const bt_device_t* device);
void bt_manager_mock_connection_established(const char* mac, const char* name);
void bt_manager_mock_connection_closed(const char* mac);
void bt_manager_mock_audio_state_changed(int state);
void bt_manager_mock_pairing_complete(const char* mac, bool success);
void bt_manager_test_reset_pending(void);
bool bt_manager_test_gap_pin_request(const char* mac);
bool bt_manager_test_gap_ssp_confirm(const char* mac, uint32_t passkey);
void bt_manager_test_gap_auth_complete(const char* mac, bool success);
int bt_manager_test_get_autostart_attempts(void);
void bt_manager_test_reset_autostart_attempts(void);
void bt_manager_test_invoke_a2dp_event(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);
#endif

#endif // BT_MANAGER_H
