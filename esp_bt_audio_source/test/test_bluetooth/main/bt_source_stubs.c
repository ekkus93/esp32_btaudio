/**
 * @file bt_source_stubs.c
 * @brief Core BT source stubs: shared state + reset/init/cleanup.
 * A2DP/GAP/scan/conn stubs split into sibling bt_source_stubs_*.c; all
 * share bt_source_stubs_internal.h.
 */
#include "bt_source_stubs_internal.h"

static const char *TAG = "BT_SOURCE_STUB";

/* Diagnostic sequence counter to trace ordering between component mock and
 * test-side stub synchronization. Incremented each time we synchronize or
 * inspect connection state so logs can be correlated across translation
 * units when running on-device. Kept local to this file. */
uint32_t s_diag_seq = 0;

/* Mock device database (capacity in bt_source_stubs_internal.h) */
bt_device_t s_devices[MAX_TEST_DEVICES];
int s_device_count = 0;

/* Mock paired device database */
bt_device_t s_paired_devices[MAX_PAIRED_DEVICES];
int s_stub_paired_device_count = 0;

/* Connection state tracking */
bt_connection_state_t s_stub_connection_state = BT_CONNECTION_STATE_DISCONNECTED;
bt_streaming_state_t s_stub_streaming_state = BT_STREAMING_STATE_STOPPED;
bt_pairing_state_t s_pairing_state = BT_PAIRING_STATE_IDLE;
bt_pairing_method_t s_pairing_method = BT_PAIRING_METHOD_NONE;
char s_connected_device_addr[18] = {0};
char s_connected_device_name[32] = {0};
char s_default_pin[8] = "1234";

/* State flags */
bool s_is_scanning = false;
bool s_auto_reconnect = false;
bool s_is_connected = false;
bool s_stub_defer_disconnect_visibility = false;
bool s_ssp_supported = true;
bool s_simulate_pairing_failure = false;
bool s_simulate_pairing_timeout = false;
bool s_test_mode = false;

/* Mock for SSP confirmation */
bool s_stub_ssp_confirmation_requested = false;
char s_stub_ssp_passkey[8] = "000000";

/* Callback types */
typedef void (*bt_connect_status_cb_t)(bt_connection_info_t* info, void* user_data);
typedef void (*bt_pairing_status_cb_t)(bt_pairing_state_t state, void* user_data);

/* Callbacks */
bt_discovery_cb_t scan_callback = NULL;
void *scan_callback_data = NULL;

/* Discovery task handle */
TaskHandle_t s_discovery_task_handle = NULL;

/* SSP confirmation passkey for testing */
uint32_t s_passkey = 123456;
bool s_waiting_for_confirmation = false;

/* connect-by-name hook: set by conn stub, read by bt_connect_device_by_name,
 * cleared by bt_mock_reset (cross-file) -> defined here, extern in header. */
const char* s_connect_by_name_address = NULL;
const char* s_connect_by_name_name = NULL;

/* Forward declaration (definition below). */
static void reset_device_database(void);

void bt_source_stub_sync_connected_state(bool connected, const char* addr, const char* name)
{
    /* Emit a compact sequence marker so we can correlate ordering in logs. */
    ESP_LOGI(TAG, "DIAG_SEQ: stub_sync #%u connected=%d addr=%s name=%s",
             (unsigned int)++s_diag_seq,
             connected,
             addr ? addr : "<null>",
             name ? name : "<null>");

    s_is_connected = connected;
    if (connected) {
        s_stub_defer_disconnect_visibility = false;
    }

    if (connected) {
        s_stub_connection_state = BT_CONNECTION_STATE_CONNECTED;
        s_stub_streaming_state = BT_STREAMING_STATE_STOPPED;

        if (addr != NULL) {
            strncpy(s_connected_device_addr, addr, sizeof(s_connected_device_addr) - 1);
            s_connected_device_addr[sizeof(s_connected_device_addr) - 1] = '\0';
        } else {
            s_connected_device_addr[0] = '\0';
        }

        if (name != NULL) {
            strncpy(s_connected_device_name, name, sizeof(s_connected_device_name) - 1);
            s_connected_device_name[sizeof(s_connected_device_name) - 1] = '\0';
        } else {
            s_connected_device_name[0] = '\0';
        }
    } else {
        s_stub_connection_state = BT_CONNECTION_STATE_DISCONNECTED;
        s_stub_streaming_state = BT_STREAMING_STATE_STOPPED;
        s_connected_device_addr[0] = '\0';
        s_connected_device_name[0] = '\0';
    }
}

/**
 * @brief Reset Bluetooth state for testing purposes
 */
BT_WEAK_FN void bt_reset_for_test(void)
{
    s_stub_defer_disconnect_visibility = false;
    s_stub_connection_state = BT_CONNECTION_STATE_DISCONNECTED;
    s_stub_streaming_state = BT_STREAMING_STATE_STOPPED;
    s_pairing_state = BT_PAIRING_STATE_IDLE;
    s_pairing_method = BT_PAIRING_METHOD_NONE;
    s_is_scanning = false;
    s_is_connected = false;
    bt_source_stub_sync_connected_state(false, NULL, NULL);

    /* Reset device database */
    reset_device_database();

    ESP_LOGI(TAG, "BT mock state reset");
}

/**
 * @brief Reset device database
 */
static void reset_device_database(void)
{
    s_device_count = 0;
    s_stub_paired_device_count = 0;
    memset(s_devices, 0, sizeof(s_devices));
    memset(s_paired_devices, 0, sizeof(s_paired_devices));
}

/* Mock function to reset state for testing */
BT_WEAK_FN void bt_mock_reset(void)
{
    ESP_LOGI(TAG, "Resetting BT mock state");
    s_stub_connection_state = BT_CONNECTION_STATE_DISCONNECTED;
    s_stub_streaming_state = BT_STREAMING_STATE_STOPPED;
    s_pairing_state = BT_PAIRING_STATE_IDLE;
    s_pairing_method = BT_PAIRING_METHOD_NONE;
    s_is_scanning = false;
    s_auto_reconnect = false;
    s_is_connected = false;
    s_ssp_supported = true;
    s_simulate_pairing_failure = false;
    s_simulate_pairing_timeout = false;
    s_waiting_for_confirmation = false;

    /* Reset device database */
    reset_device_database();

    /* Clear connection info */
    memset(s_connected_device_addr, 0, sizeof(s_connected_device_addr));
    memset(s_connected_device_name, 0, sizeof(s_connected_device_name));

    /* Reset connect-by-name hook */
    s_connect_by_name_address = NULL;
    s_connect_by_name_name = NULL;
}

/**
 * @brief Initialize Bluetooth
 */
BT_WEAK_FN esp_err_t bt_init(void)
{
    ESP_LOGI(TAG, "Initializing Bluetooth (stub)");
    
    if (s_test_mode) {
        ESP_LOGI(TAG, "Already initialized in test mode");
        return ESP_OK;
    }
    
    reset_device_database();
    
    s_stub_connection_state = BT_CONNECTION_STATE_DISCONNECTED;
    s_stub_streaming_state = BT_STREAMING_STATE_STOPPED;
    s_pairing_state = BT_PAIRING_STATE_IDLE;
    
    s_test_mode = true;
    
    /* Load any paired devices */
    bt_load_paired_devices();

    /* One-time diagnostic: log the numeric value of ESP_ERR_INVALID_STATE so we
     * can detect which esp_err.h variant the build selected (negative vs
     * positive codes). Remove after debugging. */
#ifdef DIAG_LOG
    ESP_LOGI(TAG, "DIAG: ESP_ERR_INVALID_STATE numeric value = %d (0x%08x)", (int)ESP_ERR_INVALID_STATE, (unsigned int)ESP_ERR_INVALID_STATE);
#endif
    
    ESP_LOGI(TAG, "BT initialization complete (stub)");
    /* One-time diagnostic and instrumentation removed - normal startup continues. */
    return ESP_OK;
}

/**
 * @brief Clean up Bluetooth
 */
BT_WEAK_FN void bt_cleanup(void)
{
    if (!s_test_mode) {
        return;
    }
    
    if (s_discovery_task_handle != NULL) {
        vTaskDelete(s_discovery_task_handle);
        s_discovery_task_handle = NULL;
    }
    
    s_test_mode = false;
    ESP_LOGI(TAG, "BT cleaned up (stub)");
}
