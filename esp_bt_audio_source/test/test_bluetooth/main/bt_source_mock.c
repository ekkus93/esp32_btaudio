/* bt_source_mock.c — core mock: shared state definitions, init/reset,
 * result-injection accessors, connection helpers and MAC validation.
 * A2DP/GAP/scan/conn domains split into sibling bt_source_mock_*.c; all
 * share bt_source_mock_internal.h. */
#include "bt_source_mock_internal.h"

static const char *TAG = "BT_SOURCE_MOCK";

/* Diagnostic sequence counter to trace when this mock delegates to the
 * component-level implementation. Helps correlate with the stub and
 * device-side markers during test runs. */
uint32_t s_diag_seq_mock = 0;

// Add missing state variables
bt_connection_state_t s_connection_state = BT_CONNECTION_STATE_DISCONNECTED;

/* Helper provided by bt_source_stubs.c to synchronize its internal
 * connection bookkeeping with the mock's authoritative state. Without this
 * both implementations drift, causing downstream APIs (e.g. A2DP helpers)
 * to observe stale connectivity. */
extern void bt_source_stub_sync_connected_state(bool connected, const char* addr, const char* name);


/* Constants */


mock_control_t mock_control = {
    .init_return = ESP_OK,
    .scan_start_return = ESP_OK,
    .connect_return = ESP_OK,
    .timeout_return = ESP_OK,
    .paired_devices = NULL,
    .paired_device_count = 0
};

/* Bluetooth device discovery variables */
bool s_scan_active = false;
bt_device_t s_discovered_devices[MAX_DISCOVERED_DEVICES];
int s_discovered_device_count = 0;
bt_device_type_t s_current_filter = BT_DEVICE_TYPE_ALL;  // Changed from BT_DEVICE_TYPE_UNKNOWN
TimerHandle_t s_scan_timer = NULL;

/* Bluetooth connection variables */
bool s_connected = false;
bool s_defer_disconnect_visibility = false;
bool s_initialized = false;
/*
 * Test-only: set the mock's initialized flag.
 * Keeps test-only behavior local to the mock so higher-level shims do not
 * need to call the legacy bt_init() symbol.
 */
void bt_source_mock_set_initialized(bool initialized)
{
    s_initialized = initialized ? true : false;
}
bt_connection_info_t s_current_connection;
bt_profile_t s_active_profile = BT_PROFILE_A2DP_SOURCE;  // Changed from BT_PROFILE_NONE
bool s_streaming = false;
bool s_streaming_paused = false;

/* Streaming state tracking */
bt_streaming_state_t s_streaming_state = BT_STREAMING_STATE_STOPPED;  // Changed from BT_STREAM_STATE_STOPPED

/* Pairing state and methods */
bt_pairing_state_t current_pairing_state = BT_PAIRING_STATE_IDLE;  // Changed from BT_PAIRING_STATE_NONE
bt_pairing_method_t current_pairing_method = BT_PAIRING_METHOD_NONE;  // Changed from BT_PAIRING_NONE
char current_pairing_addr[18] = {0};
char default_pin[16] = "1234"; // Default PIN
bool pin_failure_simulation = false;
bool is_pairing = false;

/* SSP pairing related variables */
bool s_ssp_support_enabled = false;  // Default: fall back to PIN unless explicitly enabled
bool s_ssp_confirmation_requested = false;
char s_ssp_passkey[7] = {0};
uint32_t s_ssp_passkey_value = 0;

/* Paired devices tracking */
bool s_device_paired[MAX_DISCOVERED_DEVICES] = {false};
int s_paired_device_count = 0;

/* Paired device storage */
bt_device_t s_stored_paired_devices[MAX_STORED_PAIRED_DEVICES];
uint8_t s_stored_paired_device_count = 0;
bool s_persistence_enabled = true;

static int find_discovered_device_index(const uint8_t addr[6])
{
    for (int i = 0; i < s_discovered_device_count; ++i) {
        if (memcmp(s_discovered_devices[i].addr, addr, sizeof(s_discovered_devices[i].addr)) == 0) {
            return i;
        }
    }
    return -1;
}

static void cache_discovered_device(const bt_device_t* device)
{
    if (!device) {
        return;
    }

    int idx = find_discovered_device_index(device->addr);
    if (idx >= 0) {
        memcpy(&s_discovered_devices[idx], device, sizeof(bt_device_t));
        s_device_paired[idx] = device->paired;
        return;
    }

    if (s_discovered_device_count >= MAX_DISCOVERED_DEVICES) {
        ESP_LOGW(TAG, "Discovered cache full; cannot track paired device %02X:%02X:%02X:%02X:%02X:%02X",
                 device->addr[0], device->addr[1], device->addr[2],
                 device->addr[3], device->addr[4], device->addr[5]);
        return;
    }

    memcpy(&s_discovered_devices[s_discovered_device_count], device, sizeof(bt_device_t));
    s_device_paired[s_discovered_device_count] = device->paired;
    s_discovered_device_count++;
}

void bt_source_mock_cache_paired_device(const bt_device_t* device)
{
    if (!device) {
        return;
    }

    bt_device_t cached = *device;
    cached.paired = true;
    cache_discovered_device(&cached);
}

/* Connection callback */
bt_connection_callback_t s_connection_callback = NULL;
void* s_connection_callback_data = NULL;


auto_reconnect_config_t s_auto_reconnect_config = {
    .auto_reconnect_enabled = false,
    .retry_count = 3,
    .retry_interval_ms = 5000
};

/* Test-only reconnect controls (CONFIG_BT_MOCK_TESTING) */
#if CONFIG_BT_MOCK_TESTING
esp_err_t s_test_reconnect_results[8];
size_t s_test_reconnect_results_len = 0;
size_t s_test_reconnect_results_idx = 0;
bool s_test_reconnect_delay_overridden = false;
uint32_t s_test_reconnect_delay_ms = 0;
#endif
uint8_t s_reconnect_attempts = 0;

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
    s_defer_disconnect_visibility = false;
    memset(&s_current_connection, 0, sizeof(s_current_connection));
    s_active_profile = BT_PROFILE_A2DP_SOURCE;  // Changed from BT_PROFILE_NONE
    s_streaming = false;
    s_streaming_paused = false;
    s_auto_reconnect_config.auto_reconnect_enabled = false;
    s_auto_reconnect_config.retry_count = 3;
    s_auto_reconnect_config.retry_interval_ms = 5000;
    /* Preserve or restore the initialized flag based on the configured
     * mock_control.init_return. Tests commonly call bt_init() followed by
     * bt_mock_reset(); resetting s_initialized to false here causes valid
     * subsequent scan/connect calls to surface ESP_ERR_INVALID_STATE (259).
     * Keep s_initialized true when the mock is configured to initialize OK. */
    s_initialized = (mock_control.init_return == ESP_OK) ? true : false;
    
    // Reset scan state
    cancel_scan_timer();
    if (s_scan_timer != NULL) {
        xTimerDelete(s_scan_timer, 0);
        s_scan_timer = NULL;
    }
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

/**
 * Set the return value for bt_init
 */
void bt_mock_set_init_return(esp_err_t ret)
{
    mock_control.init_return = ret;
}

/**
 * Set the return value for bt_scan_start
 */
void bt_mock_set_scan_start_return(esp_err_t ret)
{
    mock_control.scan_start_return = ret;
}

/**
 * Set the return value for bt_connect
 */
void bt_mock_set_connect_return(esp_err_t ret)
{
    mock_control.connect_return = ret;
}

/**
 * Set the return value for connection timeout
 */
void bt_mock_set_connect_timeout_return(esp_err_t ret)
{
    mock_control.timeout_return = ret;
}

/**
 * Set the paired devices for testing
 */
void bt_mock_set_paired_devices(bt_device_t* devices, int count)
{
    if (mock_control.paired_devices) {
        free(mock_control.paired_devices);
    }
    
    if (devices && count > 0) {
        mock_control.paired_devices = (bt_device_t*)malloc(count * sizeof(bt_device_t));
        if (mock_control.paired_devices) {
            memcpy(mock_control.paired_devices, devices, count * sizeof(bt_device_t));
            mock_control.paired_device_count = count;
        }
    } else {
        mock_control.paired_devices = NULL;
        mock_control.paired_device_count = 0;
    }
}

/**
 * Initialize the Bluetooth stack
 */
esp_err_t bt_init(void)
{
    ESP_LOGI(TAG, "Mock: Initializing Bluetooth stack");
    
    if (mock_control.init_return == ESP_OK) {
        s_initialized = true;
    }

    // If no discovered devices were seeded by the test setup, attempt to
    // call the test helper bt_mock_setup_devices() (defined in test helper
    // code) to populate some devices so scan tests have targets.
    extern void bt_mock_setup_devices(void);
    if (s_discovered_device_count == 0) {
        // It's safe to call even if the symbol resolves to a weak stub.
        bt_mock_setup_devices();
    }

    return mock_control.init_return;
}

void bt_deinit(void)
{
    /* teardown-lite: drop link and streaming, keep paired cache for
     * tests that re-init without re-pairing */
    (void)bt_scan_stop();
    s_connected = false;
    s_streaming = false;
    s_streaming_paused = false;
    s_streaming_state = BT_STREAMING_STATE_STOPPED;
}

/* Now most functions just delegate to the real implementation
esp_err_t bt_start_pairing(const char* addr)
{
    // You can add debug or instrumentation here
    ESP_LOGI(TAG, "Mock: Starting pairing with device %s", addr);
    
    // Call the real implementation
    return bt_start_pairing_real(addr);
}
*/

/**
 * Validate MAC address format
 * 
 * @param addr MAC address string to validate
 * @return true if valid, false otherwise
 */
bool is_valid_mac_address(const char* addr)
{
    if (!addr) {
        return false;
    }
    
    // Simple format check: expect exactly 17 characters (xx:xx:xx:xx:xx:xx)
    if (strlen(addr) != 17) {
        return false;
    }
    
    // Validate format with regex-like check
    for (int i = 0; i < 17; i++) {
        if ((i % 3 == 2) && (addr[i] != ':')) {
            return false;
        }
        if ((i % 3 != 2) && !((addr[i] >= '0' && addr[i] <= '9') || 
                             (addr[i] >= 'a' && addr[i] <= 'f') || 
                             (addr[i] >= 'A' && addr[i] <= 'F'))) {
            return false;
        }
    }
    
    return true;
}
#ifdef CONFIG_BT_MOCK_TESTING

/**
 * @brief Reset mock state
 */
void bt_reset_for_test(void)
{
    // Reset both stub-local state and the authoritative component mock.
    bt_source_mock_reset_impl();
    bt_source_stub_sync_connected_state(false, NULL, NULL);
    bt_mock_reset();
    bt_conn_test_reset_state();
}
#endif // CONFIG_BT_MOCK_TESTING

