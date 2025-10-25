#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <ctype.h>  // For tolower()
#include "esp_log.h"
#include "esp_err.h"
#include "bt_source.h"
#include "bt_source_mock.h"
#include "bt_mock.h"
#include "bt_api.h"

/* Ensure device-level prototypes (scan/connect/get_scan_results, etc.) are
 * visible. bt_mock.h may not expose all device-level helpers; include the
 * device header which declares bt_mock_start_scan, bt_mock_stop_scan,
 * bt_mock_get_scan_results, bt_mock_connect and the connect-by-name hook.
 */
#include "bt_mock_devices.h"

#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations for component-level mock helpers so this compilation unit
 * has prototypes when delegating to bt_mock_* functions. These match the
 * declarations in components/bluetooth/include/bt_mock_devices.h.
 */
esp_err_t bt_mock_start_pairing(const char* addr);
esp_err_t bt_mock_send_pin(const char* pin);
esp_err_t bt_mock_unpair_all_devices(void);
esp_err_t bt_mock_unpair_device(const char* addr);
/* Prefer component-provided prototypes when available */
#include "bt_mock.h"

esp_err_t bt_mock_get_paired_devices(bt_device_t *devices, uint16_t max_count, uint16_t *actual_count);
esp_err_t bt_mock_get_default_pin(char* pin, size_t size);
/* Pairing state/method helpers and paired-device query provided by component mock */
bt_pairing_state_t bt_mock_get_pairing_state(void);
bt_pairing_method_t bt_mock_get_pairing_method(void);
bool bt_mock_is_device_paired(const char* addr);

#ifdef __cplusplus
}
#endif
#endif

/* Uncomment to use real implementation directly
#include "bt_source.h"
*/

static const char *TAG = "BT_SOURCE_MOCK";

// Add missing state variables
static bt_connection_state_t s_connection_state = BT_CONNECTION_STATE_DISCONNECTED;

/* Forward declarations for internal helpers */
static bool is_valid_mac_address(const char* addr);

/* Constants */
#define MAX_DISCOVERED_DEVICES 32
#define MAX_STORED_PAIRED_DEVICES 32

/* Mock control structure for test results */
typedef struct {
    esp_err_t init_return;
    esp_err_t scan_start_return;
    esp_err_t connect_return;
    esp_err_t timeout_return;
    bt_device_t* paired_devices;
    int paired_device_count;
} mock_control_t;

static mock_control_t mock_control = {
    .init_return = ESP_OK,
    .scan_start_return = ESP_OK,
    .connect_return = ESP_OK,
    .timeout_return = ESP_OK,
    .paired_devices = NULL,
    .paired_device_count = 0
};

/* Bluetooth device discovery variables */
static bool s_scan_active = false;
static bt_device_t s_discovered_devices[MAX_DISCOVERED_DEVICES];
static int s_discovered_device_count = 0;
static bt_device_type_t s_current_filter = BT_DEVICE_TYPE_ALL;  // Changed from BT_DEVICE_TYPE_UNKNOWN

/* Bluetooth connection variables */
static bool s_connected = false;
static bool s_initialized = false;
static bt_connection_info_t s_current_connection;
static bt_profile_t s_active_profile = BT_PROFILE_A2DP_SOURCE;  // Changed from BT_PROFILE_NONE
static bool s_streaming = false;
static bool s_streaming_paused = false;

/* Streaming state tracking */
static bt_streaming_state_t s_streaming_state = BT_STREAMING_STATE_STOPPED;  // Changed from BT_STREAM_STATE_STOPPED

/* Pairing state and methods */
bt_pairing_state_t current_pairing_state = BT_PAIRING_STATE_IDLE;  // Changed from BT_PAIRING_STATE_NONE
bt_pairing_method_t current_pairing_method = BT_PAIRING_METHOD_NONE;  // Changed from BT_PAIRING_NONE
static char current_pairing_addr[18] = {0};
static char default_pin[16] = "1234"; // Default PIN
static bool pin_failure_simulation = false;
static bool is_pairing = false;

/* SSP pairing related variables */
static bool s_ssp_support_enabled = false;  // Default: fall back to PIN unless explicitly enabled
bool s_ssp_confirmation_requested = false;
static char s_ssp_passkey[7] = {0};
uint32_t s_ssp_passkey_value = 0;

/* Paired devices tracking */
static bool s_device_paired[MAX_DISCOVERED_DEVICES] = {false};
static int s_paired_device_count = 0;

/* Paired device storage */
static bt_device_t s_stored_paired_devices[MAX_STORED_PAIRED_DEVICES];
static uint8_t s_stored_paired_device_count = 0;
static bool s_persistence_enabled = true;

/* Connection callback */
static bt_connection_callback_t s_connection_callback = NULL;
static void* s_connection_callback_data = NULL;

/* Auto reconnect settings */
typedef struct {
    bool auto_reconnect_enabled;
    uint16_t retry_count;
    uint16_t retry_interval_ms;
} auto_reconnect_config_t;

static auto_reconnect_config_t s_auto_reconnect_config = {
    .auto_reconnect_enabled = false,
    .retry_count = 3,
    .retry_interval_ms = 5000
};

/* Use canonical pairing state enums from bt_source.h.
 * Avoid redefining BT_PAIRING_STATE_* macros here — the header defines
 * the authoritative values used by the tests.
 */

/**
 * Reset the mock - completely reset all variables
 */
void bt_source_mock_reset_impl(void)
{
    // Reset connection state
    s_connected = false;
    memset(&s_current_connection, 0, sizeof(s_current_connection));
    s_active_profile = BT_PROFILE_A2DP_SOURCE;  // Changed from BT_PROFILE_NONE
    s_streaming = false;
    s_streaming_paused = false;
    s_initialized = false;
    
    // Reset scan state
    s_scan_active = false;
    s_discovered_device_count = 0;
    memset(s_discovered_devices, 0, sizeof(s_discovered_devices));
    
    // Reset pairing state
    current_pairing_state = BT_PAIRING_STATE_IDLE;
    current_pairing_method = BT_PAIRING_METHOD_NONE;
    memset(current_pairing_addr, 0, sizeof(current_pairing_addr));
    strcpy(default_pin, "1234");
    pin_failure_simulation = false;
    is_pairing = false;
    
    // Reset paired devices
    s_paired_device_count = 0;
    memset(s_device_paired, 0, sizeof(s_device_paired));
    
    // Reset SSP variables
    s_ssp_support_enabled = false;
    s_ssp_confirmation_requested = false;
    memset(s_ssp_passkey, 0, sizeof(s_ssp_passkey));
    s_ssp_passkey_value = 0;
    
    // Reset persistence
    s_stored_paired_device_count = 0;
    memset(s_stored_paired_devices, 0, sizeof(s_stored_paired_devices));
    s_persistence_enabled = true;

    // Reset mock control
    mock_control.init_return = ESP_OK;
    mock_control.scan_start_return = ESP_OK;
    mock_control.connect_return = ESP_OK;
    mock_control.timeout_return = ESP_OK;
    mock_control.paired_devices = NULL;
    mock_control.paired_device_count = 0;

    // Reset connection state variable
    s_connection_state = BT_CONNECTION_STATE_DISCONNECTED;
}

/* ... (the file is long; full content taken from test_app/main/bt_source_mock.c) ... */
