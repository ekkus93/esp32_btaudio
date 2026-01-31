#include "bt_manager.h"
#include "bt_api.h"
#include "nvs_storage.h"
#include "esp_bt.h"
#include "util_safe.h"
#include "audio_processor_internal.h"
#undef TAG
#include "command_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "esp_rom_sys.h"

// Define this for ESP32 builds
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#define TAG "BT_MGR"
#else
#include "esp_log.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#define TAG "BT_MGR"
#endif

/*
 * Weak test hooks used by host tests to inject failures. Production builds
 * fall back to these no-op implementations so linking succeeds when the test
 * overrides are absent.
 */
__attribute__((weak)) int bt_manager_forced_disconnect_failure(void) { return 0; }
__attribute__((weak)) int bt_manager_forced_start_failure(void) { return 0; }
__attribute__((weak)) int bt_manager_forced_stop_failure(void) { return 0; }

// Private data
static struct {
    bool initialized;
    char device_name[32];
    bool scanning;
    bool connected;
    bool audio_playing;
    int volume;
    bt_device_list_t discovered_devices;
    bt_device_list_t paired_devices;
    bt_connected_cb connected_callback;
    bt_disconnected_cb disconnected_callback;
    char connected_mac[18];
    char connected_name[32];
} bt_ctx = {
    .initialized = false,
    .scanning = false,
    .connected = false,
    .audio_playing = false,
    .volume = 75
};

/* Runtime preference: automatically issue START once connected/when audio is
 * requested so the sink begins draining without manual intervention. */
static bool s_autostart_enabled = true;

typedef struct {
    bool pin_pending;
    bool ssp_pending;
    esp_bd_addr_t bda;
    char mac[18];
    uint32_t passkey;
} bt_pairing_pending_t;

#define safe_vsnprintf util_safe_vsnprintf
#define safe_snprintf util_safe_snprintf
#define safe_copy_str util_safe_copy_str
#define safe_memcpy util_safe_memcpy
#define safe_memset util_safe_memset
#define parse_mac_bytes util_parse_mac

static bt_pairing_pending_t s_pair_pending = {0};

#if CONFIG_BT_MOCK_TESTING
#define BT_SOURCE_SKIP_DEVICE_STRUCT 1
#include "bt_source.h"
#undef BT_SOURCE_SKIP_DEVICE_STRUCT
/* Forward declarations from the Bluetooth abstraction so we can delegate
 * pairing to the mock implementation when mock testing configuration is
 * enabled. These symbols live in the bt_mock component. */
esp_err_t bt_start_pairing(const char* addr);
esp_err_t bt_mock_send_pin(const char* pin);
bt_pairing_method_t bt_mock_get_pairing_method(void);
esp_err_t bt_mock_get_ssp_passkey(char* passkey, size_t size);
#endif

static void bt_pairing_format_mac(const esp_bd_addr_t bda, char* out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    safe_snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
                  bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

static void bt_pairing_set_pending_addr(const esp_bd_addr_t bda)
{
    safe_memcpy(s_pair_pending.bda, sizeof(s_pair_pending.bda), bda, sizeof(esp_bd_addr_t));
    bt_pairing_format_mac(bda, s_pair_pending.mac, sizeof(s_pair_pending.mac));
}

static void bt_pairing_clear_pending_flags(bool clear_pin, bool clear_ssp)
{
    if (clear_pin) {
        s_pair_pending.pin_pending = false;
    }
    if (clear_ssp) {
        s_pair_pending.ssp_pending = false;
    }
    if (!s_pair_pending.pin_pending && !s_pair_pending.ssp_pending) {
        safe_memset(s_pair_pending.bda, 0, sizeof(s_pair_pending.bda));
        s_pair_pending.mac[0] = '\0';
        s_pair_pending.passkey = 0;
    }
}

static bool bt_pairing_parse_mac_string(const char* mac, esp_bd_addr_t out)
{
    return parse_mac_bytes(mac, out);
}

static bool bt_pairing_addr_is_zero(const esp_bd_addr_t addr)
{
    if (!addr) {
        return true;
    }
    for (size_t i = 0; i < sizeof(esp_bd_addr_t); ++i) {
        if (addr[i] != 0) {
            return false;
        }
    }
    return true;
}

static void bt_pairing_prepare_pending_for_event(const esp_bd_addr_t addr)
{
    if (!addr) {
        return;
    }

    if (!bt_pairing_addr_is_zero(s_pair_pending.bda) &&
        memcmp(s_pair_pending.bda, addr, sizeof(esp_bd_addr_t)) != 0) {
        char prev_mac[18] = {0};
        char new_mac[18] = {0};
        bt_pairing_format_mac(s_pair_pending.bda, prev_mac, sizeof(prev_mac));
        bt_pairing_format_mac(addr, new_mac, sizeof(new_mac));
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG, "Pairing target changed from %s to %s due to GAP event", prev_mac, new_mac);
#endif
        bt_pairing_clear_pending_flags(true, true);
    }

    bt_pairing_set_pending_addr(addr);
    s_pair_pending.passkey = 0;
}

#ifdef UNIT_TEST
__attribute__((weak)) void bt_manager_test_record_pair_event(const char* subtype, const char* data) {
    (void)subtype;
    (void)data;
}
#endif

static void bt_pairing_send_event(const char* subtype, const char* data)
{
    if (!subtype || !data) {
        return;
    }

#if defined(ESP_PLATFORM)
    cmd_send_event_pair(subtype, data);
#elif defined(UNIT_TEST)
    bt_manager_test_record_pair_event(subtype, data);
#endif
}

static void bt_gap_handle_pin_req(const esp_bd_addr_t bda)
{
    char bda_str[18];
    safe_snprintf(bda_str, sizeof(bda_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                  bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

    ESP_LOGI(TAG, "PIN request from device: %s", bda_str);
    bt_pairing_prepare_pending_for_event(bda);
    s_pair_pending.pin_pending = true;
    s_pair_pending.ssp_pending = false;
    s_pair_pending.passkey = 0;
    bt_pairing_send_event("PIN_REQUEST", bda_str);
}

static void bt_gap_handle_ssp_confirm(const esp_bd_addr_t bda, uint32_t passkey)
{
    char bda_str[18];
    safe_snprintf(bda_str, sizeof(bda_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                  bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

    char data[64];
    safe_snprintf(data, sizeof(data), "%s,%u", bda_str, (unsigned int)passkey);
    ESP_LOGI(TAG, "SSP confirm request from %s value=%u", bda_str, (unsigned int)passkey);
    bt_pairing_prepare_pending_for_event(bda);
    s_pair_pending.ssp_pending = true;
    s_pair_pending.pin_pending = false;
    s_pair_pending.passkey = passkey;
    bt_pairing_send_event("CONFIRM", data);
}

static void bt_gap_handle_auth_cmpl(const esp_bd_addr_t bda, esp_bt_status_t stat)
{
    char bda_str[18] = {0};
    safe_snprintf(bda_str, sizeof(bda_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                  bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

    bt_pairing_prepare_pending_for_event(bda);
    if (stat == ESP_BT_STATUS_SUCCESS) {
        ESP_LOGI(TAG, "Authentication (pairing) successful: %s", bda_str);
        bt_pairing_send_event("SUCCESS", bda_str);
#if defined(ESP_PLATFORM)
        char dev_name[32] = {0};
        for (int i = 0; i < bt_ctx.discovered_devices.count; i++) {
            if (strcmp(bt_ctx.discovered_devices.devices[i].mac, bda_str) == 0) {
                safe_copy_str(dev_name, sizeof(dev_name), bt_ctx.discovered_devices.devices[i].name);
                break;
            }
        }
        nvs_storage_add_paired_device(bda_str, dev_name[0] ? dev_name : NULL);
#endif
    } else {
        ESP_LOGW(TAG, "Authentication (pairing) failed: %s", bda_str);
        bt_pairing_send_event("FAILED", bda_str);
    }

    bt_pairing_clear_pending_flags(true, true);
}

#if defined(ESP_PLATFORM) || defined(UNIT_TEST)
#if defined(__GNUC__)
/* Forward-declare the connection manager callbacks so the manager can
 * forward A2DP events it receives. This keeps the connection manager's
 * internal state in sync even when the manager registers the A2DP
 * callback (some platforms only allow a single A2DP callback). */
extern void bt_connection_state_cb(esp_a2d_connection_state_t state, esp_bd_addr_t bd_addr);
extern void bt_audio_state_cb(esp_a2d_audio_state_t state, esp_bd_addr_t bd_addr);
#else
extern void bt_connection_state_cb(esp_a2d_connection_state_t state, esp_bd_addr_t bd_addr);
extern void bt_audio_state_cb(esp_a2d_audio_state_t state, esp_bd_addr_t bd_addr);
#endif
#endif

#ifdef ESP_PLATFORM
#include <inttypes.h>
#include "nvs_flash.h"
// Callback declarations
static void bt_app_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
static void bt_app_a2d_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);
static void bt_app_avrc_ct_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);
// esp_a2d_source_data_cb_t signature: int32_t (*)(uint8_t *buf, int32_t len)
static int32_t bt_app_a2d_data_callback(uint8_t *buf, int32_t len);
static esp_err_t bt_manager_init_profiles(void);
// Audio processor API - used by A2DP data callback to pull PCM
#include "audio_processor.h"
#endif

#ifdef UNIT_TEST
__attribute__((weak)) void bt_manager_test_record_unpair(const char* mac) {
    (void)mac;
}

__attribute__((weak)) int bt_manager_test_should_force_unpair_failure(void) { return 0; }

__attribute__((weak)) int bt_manager_test_should_force_unpair_all_failure(void) { return 0; }

__attribute__((weak)) void bt_manager_test_record_unpair_all_call(int cleared_before, int removed) {
    (void)cleared_before;
    (void)removed;
}

// Unit-test hook: record when a scan is started. Provided by host mocks.
__attribute__((weak)) void bt_manager_test_record_scan_start(void) { }
#
// Unit-test hook: record when a pairing attempt is started. Provided by host mocks.
__attribute__((weak)) void bt_manager_test_record_pair_start(const char* mac) { (void)mac; }
static int s_autostart_attempts = 0;

int bt_manager_test_get_autostart_attempts(void) {
    return s_autostart_attempts;
}

void bt_manager_test_reset_autostart_attempts(void) {
    s_autostart_attempts = 0;
}

void bt_manager_test_reset_pending(void)
{
    bt_pairing_clear_pending_flags(true, true);
}

bool bt_manager_test_gap_pin_request(const char* mac)
{
    esp_bd_addr_t addr = {0};
    if (!bt_pairing_parse_mac_string(mac, addr)) {
        return false;
    }
    bt_gap_handle_pin_req(addr);
    return true;
}

bool bt_manager_test_gap_ssp_confirm(const char* mac, uint32_t passkey)
{
    esp_bd_addr_t addr = {0};
    if (!bt_pairing_parse_mac_string(mac, addr)) {
        return false;
    }
    bt_gap_handle_ssp_confirm(addr, passkey);
    return true;
}

void bt_manager_test_gap_auth_complete(const char* mac, bool success)
{
    esp_bd_addr_t addr = {0};
    if (!bt_pairing_parse_mac_string(mac, addr)) {
        return;
    }
    bt_gap_handle_auth_cmpl(addr, success ? ESP_BT_STATUS_SUCCESS : ESP_BT_STATUS_AUTH_FAILURE);
}
#endif

// Initialize Bluetooth Manager
 bt_err_t bt_manager_init(const bt_manager_init_t* config) {
    if (config == NULL || config->device_name == NULL) {
        return ESP_FAIL;
    }
    
    if (bt_ctx.initialized) {
        return ESP_OK; // Already initialized
    }

    /* Reset runtime defaults at each init so per-session overrides (like
     * autostart disable) do not leak across init/deinit cycles. */
    s_autostart_enabled = true;
#if defined(UNIT_TEST)
    s_autostart_attempts = 0;
#endif
    
    // Store configuration
    safe_copy_str(bt_ctx.device_name, sizeof(bt_ctx.device_name), config->device_name);
    bt_ctx.connected_callback = config->connected_cb;
    bt_ctx.disconnected_callback = config->disconnected_cb;
    
    // Initialize structures
    safe_memset(&bt_ctx.discovered_devices, 0, sizeof(bt_ctx.discovered_devices));
    safe_memset(&bt_ctx.paired_devices, 0, sizeof(bt_ctx.paired_devices));
    
#ifdef ESP_PLATFORM
    // NVS is initialized by main.c before calling bt_manager_init.
    // bt_manager assumes NVS is ready and uses nvs_storage_* functions.

    // Initialize Bluetooth controller
    esp_err_t ret;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.mode = ESP_BT_MODE_CLASSIC_BT;
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialize controller failed: %s (%d)", esp_err_to_name(ret), (int)ret);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Controller initialized via bt_manager: mode=%d target_mode=0x%x", bt_cfg.mode, ESP_BT_MODE_CLASSIC_BT);

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Enable controller failed: %s (%d)", esp_err_to_name(ret), (int)ret);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Controller enabled via bt_manager: mode=CLASSIC_BT");

    // Initialize Bluedroid
    if ((ret = esp_bluedroid_init()) != ESP_OK) {
        ESP_LOGE(TAG, "Initialize bluedroid failed: %s", esp_err_to_name(ret));
    return ESP_FAIL;
    }

    if ((ret = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(TAG, "Enable bluedroid failed: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    // Configure device name
    // Use GAP API (not deprecated) to set the device name. esp_bt_dev_set_device_name is deprecated.
    esp_err_t _err_name = esp_bt_gap_set_device_name(config->device_name);
    if (_err_name != ESP_OK) {
        ESP_LOGW(TAG, "esp_bt_gap_set_device_name failed (%s)", esp_err_to_name(_err_name));
    }

    // Register GAP callback
    esp_bt_gap_register_callback(bt_app_gap_callback);

    if ((ret = bt_manager_init_profiles()) != ESP_OK) {
        return ret;
    }

    // Set device discoverable and connectable
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    
    ESP_LOGI(TAG, "Bluetooth manager initialized with name: %s", config->device_name);
    
    // Load persisted paired devices from NVS (NVS already initialized by main.c)
    int count = 0;
    if (nvs_storage_get_paired_count(&count) == ESP_OK && count > 0) {
        for (int i = 0; i < count && bt_ctx.paired_devices.count < 20; i++) {
            char mac[32] = {0};
            char name[32] = {0};
            /* Some build configurations may not use 'name' visibly
             * (e.g., stripped logging). Mark as used to avoid
             * -Wunused-variable warnings while preserving logic.
             */
            (void)name;
            if (nvs_storage_get_paired_device_by_index(i, mac, sizeof(mac), name, sizeof(name)) == ESP_OK) {
                int idx = bt_ctx.paired_devices.count;
                safe_copy_str(bt_ctx.paired_devices.devices[idx].mac,
                              sizeof(bt_ctx.paired_devices.devices[idx].mac), mac);
                if (name[0]) {
                    safe_copy_str(bt_ctx.paired_devices.devices[idx].name,
                                  sizeof(bt_ctx.paired_devices.devices[idx].name), name);
                }
                bt_ctx.paired_devices.count++;
            }
        }
        ESP_LOGI(TAG, "Loaded %d persisted paired devices", bt_ctx.paired_devices.count);
    }
#endif
    
    bt_ctx.initialized = true;
    return ESP_OK;
}

// Deinitialize Bluetooth Manager
 bt_err_t bt_manager_deinit(void) {
    if (!bt_ctx.initialized) {
        return ESP_FAIL;
    }
    
#ifdef ESP_PLATFORM
    // Stop scanning if active
    if (bt_ctx.scanning) {
        bt_stop_scan();
    }
    
    // Disconnect if connected
    if (bt_ctx.connected) {
        bt_disconnect();
    }
    
    // Deinitialize A2DP
    esp_a2d_source_deinit();
    
    // Disable and deinitialize Bluetooth
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    
    ESP_LOGI(TAG, "Bluetooth manager deinitialized");
#endif
    
    bt_ctx.initialized = false;
    bt_ctx.connected = false;
    bt_ctx.audio_playing = false;
    // Optionally reset other fields if needed

    return ESP_OK;
}

// Expose a simple integer getter so other subsystems (command interface)
// can query the manager's connected flag without depending on internal
// structures. Return 1 when connected, 0 otherwise.
int bt_manager_is_connected(void) {
    return bt_ctx.connected ? 1 : 0;
}

// Start device scanning
 bt_err_t bt_start_scan(void) {
    if (!bt_ctx.initialized) {
        return ESP_FAIL;
    }
    
    if (bt_ctx.scanning) {
        return ESP_OK; // Already scanning
    }
    
    // Clear previous discovered devices
    safe_memset(&bt_ctx.discovered_devices, 0, sizeof(bt_ctx.discovered_devices));
    
#ifdef ESP_PLATFORM
    // Start discovery
    if (esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Start device discovery failed");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Started Bluetooth device scanning");
#endif
    
    bt_ctx.scanning = true;
    /* Unit-test hook: notify test shim that a scan started so host tests can
     * assert the command layer invoked the manager. The test shim provides
     * bt_manager_test_record_scan_start() in the mocks; declare as weak
     * and call it here so only unit-test builds attempt the symbol. */
#ifdef UNIT_TEST
    /* The weak symbol is declared at file scope (above); call it here so
     * host-mode tests can observe that a scan was initiated. Using an
     * extern declaration avoids redeclaring the weak attribute inside the
     * function (which some toolchains reject). */
    extern void bt_manager_test_record_scan_start(void);
    bt_manager_test_record_scan_start();
#endif

    return ESP_OK;
}

// Stop device scanning
 bt_err_t bt_stop_scan(void) {
    if (!bt_ctx.initialized) {
        return ESP_FAIL;
    }
    
    if (!bt_ctx.scanning) {
        return ESP_OK; // Not scanning
    }
    
#ifdef ESP_PLATFORM
    // Stop discovery
    if (esp_bt_gap_cancel_discovery() != ESP_OK) {
        ESP_LOGE(TAG, "Stop device discovery failed");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Stopped Bluetooth device scanning");
#endif
    
    bt_ctx.scanning = false;
    return ESP_OK;
}
    /* (scan test hook moved into bt_start_scan()) */

// Connect to a device
 bt_err_t bt_connect(const char* mac) {
    if (!bt_ctx.initialized) {
        return ESP_FAIL;
    }
    
    if (mac == NULL) {
        return ESP_FAIL;
    }
    
    if (bt_ctx.connected) {
        return ESP_FAIL;
    }
    
#ifdef ESP_PLATFORM
    // Convert MAC string to address
    esp_bd_addr_t bda;
    if (!parse_mac_bytes(mac, bda)) {
        ESP_LOGE(TAG, "Invalid MAC address format: %s", mac);
    return ESP_FAIL;
    }
    
    // Connect to device
    if (esp_a2d_source_connect(bda) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to device: %s", mac);
    return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Connecting to device: %s", mac);
    safe_copy_str(bt_ctx.connected_mac, sizeof(bt_ctx.connected_mac), mac);
#endif
    
    return ESP_OK;
}

// Connect to a device by name
 bt_err_t bt_connect_by_name(const char* name) {
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find device in discovered devices
    for (int i = 0; i < bt_ctx.discovered_devices.count; i++) {
        if (strcmp(bt_ctx.discovered_devices.devices[i].name, name) == 0) {
            return bt_connect(bt_ctx.discovered_devices.devices[i].mac);
        }
    }
    
    // Not found in discovered list — try paired devices (persisted)
    for (int i = 0; i < bt_ctx.paired_devices.count; i++) {
        if (strcmp(bt_ctx.paired_devices.devices[i].name, name) == 0) {
            return bt_connect(bt_ctx.paired_devices.devices[i].mac);
        }
    }
    
    return ESP_FAIL;
}

// Disconnect from the current device
#if defined(UNIT_TEST)
// Allow test code to override the manager disconnect implementation by
// marking this symbol weak in unit test builds. This keeps production
// behavior unchanged while allowing test-only mocks/stubs in
// `test_app` to take precedence during linking.
__attribute__((weak))
bt_err_t bt_disconnect(void) {
#else
 bt_err_t bt_disconnect(void) {
#endif
#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
    const char *task_name = pcTaskGetName(NULL);
    ESP_LOGI(TAG,
             "DIAG: mgr_bt_disconnect entry task=\"%s\" initialized=%d connected=%d audio_playing=%d mac=\"%s\"",
             task_name ? task_name : "<unknown>",
             bt_ctx.initialized,
             bt_ctx.connected,
             bt_ctx.audio_playing,
             bt_ctx.connected ? bt_ctx.connected_mac : "<none>");
#endif
    if (!bt_ctx.initialized) {
        /* Treat disconnect when manager is not initialized as a no-op
         * returning ESP_OK so higher-level C wrappers that convert
         * bt_err_t to int (0 == success) do not leak non-canonical
         * positive values into tests. This makes disconnect idempotent
         * across initialization state.
         */
#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
        ESP_LOGI(TAG, "DIAG: mgr_bt_disconnect short-circuit (manager not initialized)");
#endif
        return ESP_OK;
    }
    
    if (!bt_ctx.connected) {
#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
        ESP_LOGI(TAG, "DIAG: mgr_bt_disconnect short-circuit (already disconnected)");
#endif
        return ESP_OK; // Already disconnected
    }
    
#ifdef ESP_PLATFORM
    // Stop audio if playing
    if (bt_ctx.audio_playing) {
#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
        ESP_LOGI(TAG, "DIAG: mgr_bt_disconnect stopping audio before disconnect");
#endif
        bt_stop_audio();
    }
    
    // Convert MAC string to address
    esp_bd_addr_t bda;
        if (!parse_mac_bytes(bt_ctx.connected_mac, bda)) {
        ESP_LOGE(TAG, "Invalid MAC format in stored address: %s", bt_ctx.connected_mac);
#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
        ESP_LOGE(TAG, "DIAG: mgr_bt_disconnect aborting due to invalid MAC string");
#endif
        return ESP_ERR_INVALID_ARG;
    }
    
    // Disconnect device
    if (esp_a2d_source_disconnect(bda) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disconnect from device: %s", bt_ctx.connected_mac);
#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
        ESP_LOGE(TAG, "DIAG: mgr_bt_disconnect esp_a2d_source_disconnect() returned error");
#endif
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Disconnecting from device: %s", bt_ctx.connected_mac);
#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
    ESP_LOGI(TAG,
             "DIAG: mgr_bt_disconnect invoked esp_a2d_source_disconnect(), bt_ctx.connected=%d",
             bt_ctx.connected);
#endif
#else
    // For testing without ESP-IDF
    bt_ctx.connected = false;
    
    if (bt_ctx.disconnected_callback != NULL) {
        bt_ctx.disconnected_callback(bt_ctx.connected_mac);
    }
#if defined(CONFIG_BT_MOCK_TESTING)
    printf("DIAG: mgr_bt_disconnect (mock build) set connected flag to %d\n", bt_ctx.connected);
#endif
#endif
    
#if defined(ESP_PLATFORM) && defined(CONFIG_BT_MOCK_TESTING)
    ESP_LOGI(TAG,
             "DIAG: mgr_bt_disconnect exit connected=%d audio_playing=%d mac=\"%s\"",
             bt_ctx.connected,
             bt_ctx.audio_playing,
             bt_ctx.connected ? bt_ctx.connected_mac : "<none>");
#endif
    return ESP_OK;
}

// Get the list of discovered devices
bt_device_list_t* bt_get_device_list(void) {
    if (!bt_ctx.initialized) {
        return NULL;
    }
    
    return &bt_ctx.discovered_devices;
}

// Get the list of paired devices
bt_device_list_t* bt_get_paired_devices(void) {
    if (!bt_ctx.initialized) {
        return NULL;
    }
    
    return &bt_ctx.paired_devices;
}

// Stop audio streaming
bt_err_t bt_stop_audio(void) {
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!bt_ctx.audio_playing) {
        return ESP_OK; // Not playing
    }

#ifdef ESP_PLATFORM
    // Stop audio stream
    if (esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to suspend audio stream");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Suspended audio streaming");
#else
    // For testing without ESP-IDF
    bt_ctx.audio_playing = false;
#endif
    
    return ESP_OK;
}

void bt_manager_set_autostart_enabled(bool enable) {
    s_autostart_enabled = enable;
    ESP_LOGI(TAG, "bt_manager: autostart %s", enable ? "ENABLED" : "DISABLED");
}

bool bt_manager_is_autostart_enabled(void) {
    return s_autostart_enabled;
}

// Start audio streaming
 bt_err_t bt_start_audio(void) {
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!bt_ctx.connected) {
        return ESP_ERR_INVALID_STATE;
    }

    if (bt_ctx.audio_playing) {
        return ESP_OK; // Already playing
    }

#ifdef ESP_PLATFORM
    /* Stronger safety checks: ensure not only that total free DRAM looks
     * reasonable but also that a sufficiently large contiguous block is
     * available and that some internal (MALLOC_CAP_INTERNAL) memory exists.
     * This reduces false positives where total free bytes are sufficient
     * but fragmentation or internal-heap scarcity causes later allocations
     * (inside the BT stack/codec) to fail. */
    {
        const size_t DRAM_THRESHOLD = 64 * 1024; /* require >=64KiB total free */
        const size_t LARGEST_REQUIRED = 32 * 1024; /* require a 32KiB contiguous block */
        const size_t INTERNAL_REQUIRED = 8 * 1024; /* prefer >=8KiB internal-capable */

        size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
        size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
        size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

        if (free_dram <= DRAM_THRESHOLD || largest < LARGEST_REQUIRED || internal_free < INTERNAL_REQUIRED) {
            ESP_LOGW(TAG, "bt_start_audio: skipping start due to low memory: free_dram=%zu largest=%zu internal_free=%zu; not starting A2DP", free_dram, largest, internal_free);
            return ESP_ERR_NO_MEM;
        }
    }

    // Start audio stream
    if (esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start audio stream");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Started audio streaming");
#else
    // For testing without ESP-IDF
    bt_ctx.audio_playing = true;
#endif
    
    return ESP_OK;
}

// Set volume level
 bt_err_t bt_set_volume(int volume) {
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (volume < 0 || volume > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    
    bt_ctx.volume = volume;
    
#ifdef ESP_PLATFORM
    // Volume control would be implemented here if supported
    // Currently not directly supported by ESP-IDF A2DP API
    ESP_LOGI(TAG, "Volume set to %d%%", volume);
#endif
    
    return ESP_OK;
}

// Pair with a device
 bt_err_t bt_pair(const char* mac) {
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
#ifdef ESP_PLATFORM
    // Convert MAC string to address
    esp_bd_addr_t bda;
    if (!bt_pairing_parse_mac_string(mac, bda)) {
        ESP_LOGE(TAG, "Invalid MAC address format: %s", mac);
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_BT_MOCK_TESTING
    /* Under the mock-testing configuration use the higher-level pairing helper
     * so tests exercise the mock's deterministic flow instead of attempting to
     * touch the real controller. Capture the pending pairing state so command
     * handlers can accept PIN submissions later. */
    esp_err_t mock_err = bt_start_pairing(mac);
    if (mock_err == ESP_OK) {
        bt_pairing_clear_pending_flags(true, true);
        bt_pairing_set_pending_addr(bda);
        bt_pairing_method_t method = bt_mock_get_pairing_method();
        if (method == BT_PAIRING_METHOD_SSP) {
            s_pair_pending.ssp_pending = true;
            char passkey_str[16] = {0};
            if (bt_mock_get_ssp_passkey(passkey_str, sizeof(passkey_str)) == ESP_OK) {
                s_pair_pending.passkey = (uint32_t)strtoul(passkey_str, NULL, 10);
            } else {
                s_pair_pending.passkey = 0;
            }
        } else if (method == BT_PAIRING_METHOD_PIN) {
            s_pair_pending.pin_pending = true;
            s_pair_pending.passkey = 0;
        } else {
            s_pair_pending.passkey = 0;
        }
    }
    return mock_err;
#endif

    // Stop an active discovery session; some controllers reject bonding requests
    // while inquiry is running.
    if (bt_ctx.scanning) {
        if (esp_bt_gap_cancel_discovery() == ESP_OK) {
            bt_ctx.scanning = false;
        }
    }

    // Short-circuit if the controller already holds a bond for this device.
    int bonded = esp_bt_gap_get_bond_device_num();
    if (bonded > 0) {
        esp_bd_addr_t bonded_list[20] = {0};
        int list_capacity = bonded;
        if (list_capacity > (int)(sizeof(bonded_list) / sizeof(bonded_list[0]))) {
            list_capacity = sizeof(bonded_list) / sizeof(bonded_list[0]);
        }
        if (esp_bt_gap_get_bond_device_list(&list_capacity, bonded_list) == ESP_OK) {
            for (int i = 0; i < list_capacity; ++i) {
                if (memcmp(bonded_list[i], bda, sizeof(esp_bd_addr_t)) == 0) {
                    ESP_LOGI(TAG, "Device %s already bonded; skipping pairing", mac);
                    return ESP_OK;
                }
            }
        }
    }

    bt_pairing_clear_pending_flags(true, true);
    bt_pairing_set_pending_addr(bda);
    s_pair_pending.passkey = 0;

    // Initiate GAP-level bonding by requesting the remote service list. The
    // controller will establish an ACL link and drive the authentication flow,
    // delivering PIN/SSP callbacks via bt_app_gap_callback.
    esp_err_t gap_err = esp_bt_gap_get_remote_services(bda);
    if (gap_err == ESP_OK) {
        ESP_LOGI(TAG, "Initiated GAP service discovery to pair with %s", mac);
        return ESP_OK;
    }

    // Fall back to reading the remote name which also triggers an ACL link and
    // will still surface the required authentication callbacks.
    gap_err = esp_bt_gap_read_remote_name(bda);
    if (gap_err == ESP_OK) {
        ESP_LOGI(TAG, "Requested remote name to pair with %s", mac);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to initiate GAP bonding for %s: %s", mac, esp_err_to_name(gap_err));
    bt_pairing_clear_pending_flags(true, true);
    return gap_err;
#else
    esp_bd_addr_t bda = {0};
    if (!bt_pairing_parse_mac_string(mac, bda)) {
        return ESP_ERR_INVALID_ARG;
    }
    bt_pairing_clear_pending_flags(true, true);
    bt_pairing_set_pending_addr(bda);
    s_pair_pending.passkey = 0;
    return ESP_OK;
#endif
}

// Unpair a device
 bt_err_t bt_unpair(const char* mac) {
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
#ifdef UNIT_TEST
    if (bt_manager_test_should_force_unpair_failure()) {
        return ESP_FAIL;
    }
#endif

#ifdef ESP_PLATFORM
    esp_bd_addr_t bda = {0};
    if (!bt_pairing_parse_mac_string(mac, bda)) {
        ESP_LOGE(TAG, "Invalid MAC address format: %s", mac);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = esp_bt_gap_remove_bond_device(bda);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove controller bond for %s: %s", mac, esp_err_to_name(result));
        goto exit;
    }

    esp_err_t storage_err = nvs_storage_remove_paired_device(mac);
    if (storage_err == ESP_OK) {
        ESP_LOGI(TAG, "Unpaired device: %s", mac);
        result = ESP_OK;
    } else {
        const char* err_name = esp_err_to_name(storage_err);
        if (storage_err == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Controller bond for %s removed, but NVS entry missing (%s)", mac, err_name);
        } else {
            ESP_LOGE(TAG, "Failed to purge NVS entry for %s: %s", mac, err_name);
        }
        result = storage_err;
        goto exit;
    }
#else
    esp_err_t result = nvs_storage_remove_paired_device(mac);
#endif

exit:
#ifdef UNIT_TEST
    bt_manager_test_record_unpair(mac);
#endif

    return result;
}

// Unpair all devices
 bt_err_t bt_unpair_all(void) {
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t controller_status = ESP_OK;
    int removed_count = 0;

#ifdef UNIT_TEST
    if (bt_manager_test_should_force_unpair_all_failure()) {
        return ESP_FAIL;
    }
#endif

#ifdef ESP_PLATFORM
    int bonded_devices = esp_bt_gap_get_bond_device_num();
    if (bonded_devices < 0) {
        ESP_LOGW(TAG, "Unable to query bonded device count");
        controller_status = ESP_FAIL;
    } else if (bonded_devices > 0) {
        int list_capacity = bonded_devices;
        esp_bd_addr_t *bond_list = (esp_bd_addr_t *)calloc((size_t)list_capacity, sizeof(esp_bd_addr_t));
        if (!bond_list) {
            ESP_LOGE(TAG, "Insufficient memory to retrieve bonded device list");
            return ESP_ERR_NO_MEM;
        }

        esp_err_t list_err = esp_bt_gap_get_bond_device_list(&list_capacity, bond_list);
        if (list_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to fetch bonded devices: %s", esp_err_to_name(list_err));
            controller_status = list_err;
        } else {
            for (int i = 0; i < list_capacity; ++i) {
                esp_err_t err = esp_bt_gap_remove_bond_device(bond_list[i]);
                if (err == ESP_OK) {
                    removed_count++;
                } else {
                    if (controller_status == ESP_OK) {
                        controller_status = err;
                    }
                    char mac[18] = {0};
                    bt_pairing_format_mac(bond_list[i], mac, sizeof(mac));
                    ESP_LOGE(TAG, "Failed to remove bond for %s: %s", mac, esp_err_to_name(err));
                }
            }
        }

        free(bond_list);
    }

    if (controller_status == ESP_OK) {
        ESP_LOGI(TAG, "Removed %d bonded device(s) from controller", removed_count);
    }
#endif

    int cleared_before = 0;
    (void)nvs_storage_get_paired_count(&cleared_before);

    esp_err_t storage_err = nvs_storage_clear_paired_devices();
    if (storage_err != ESP_OK && storage_err != ESP_ERR_NOT_FOUND) {
        if (controller_status == ESP_OK) {
            controller_status = storage_err;
        }
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Failed to clear paired devices from NVS: %s", esp_err_to_name(storage_err));
#endif
    }

    safe_memset(&bt_ctx.paired_devices, 0, sizeof(bt_ctx.paired_devices));

#ifdef UNIT_TEST
    bt_manager_test_record_unpair_all_call(cleared_before, removed_count);
#endif

    return controller_status;
}

// Set PIN code for pairing
 bt_err_t bt_set_pin(const char* pin) {
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (pin == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
#ifdef ESP_PLATFORM
    // Set PIN code
    esp_bt_pin_code_t pin_code;
    size_t pin_len = strlen(pin);
    if (pin_len > ESP_BT_PIN_CODE_LEN) {
        pin_len = ESP_BT_PIN_CODE_LEN;
    }
    safe_memcpy(pin_code, pin_len, pin, pin_len);
    esp_bt_pin_type_t pin_type = (pin_len == 16) ? ESP_BT_PIN_TYPE_VARIABLE : ESP_BT_PIN_TYPE_FIXED;
    
    esp_bt_gap_set_pin(pin_type, pin_len, pin_code);
    ESP_LOGI(TAG, "PIN code set to: %s, length: %d", pin, pin_len);
#endif
    
    return ESP_OK;
}

bool bt_pairing_get_pending_request(bt_pairing_request_info_t* info)
{
    if (!info) {
        return false;
    }
    if (!s_pair_pending.pin_pending && !s_pair_pending.ssp_pending) {
        safe_memset(info, 0, sizeof(*info));
        return false;
    }
    info->pin_request_pending = s_pair_pending.pin_pending;
    info->ssp_confirm_pending = s_pair_pending.ssp_pending;
    safe_copy_str(info->mac, sizeof(info->mac), s_pair_pending.mac);
    info->mac[sizeof(info->mac) - 1] = '\0';
    info->passkey = s_pair_pending.passkey;
    return true;
}

bt_err_t bt_pairing_confirm(const char* mac, bool accept)
{
    (void)mac;
    (void)accept;
#ifdef ESP_PLATFORM
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_bd_addr_t target = {0};

    if (mac && mac[0] != '\0') {
        if (!bt_pairing_parse_mac_string(mac, target)) {
            return ESP_ERR_INVALID_ARG;
        }
    } else if (s_pair_pending.ssp_pending) {
        safe_memcpy(target, sizeof(target), s_pair_pending.bda, sizeof(target));
    } else {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_bt_gap_ssp_confirm_reply(target, accept);
    if (err == ESP_OK) {
        bt_pairing_clear_pending_flags(false, true);
    }
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bt_err_t bt_pairing_submit_pin(const char* mac, const char* pin)
{
#ifdef ESP_PLATFORM
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pin == NULL || pin[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_BT_MOCK_TESTING
    esp_bd_addr_t target = {0};
    char formatted_mac[18] = {0};
    const char* mac_to_emit = NULL;

    if (mac && mac[0] != '\0') {
        if (!bt_pairing_parse_mac_string(mac, target)) {
            return ESP_ERR_INVALID_ARG;
        }
        bt_pairing_format_mac(target, formatted_mac, sizeof(formatted_mac));
        mac_to_emit = formatted_mac;
    } else if (s_pair_pending.pin_pending) {
        safe_memcpy(target, sizeof(target), s_pair_pending.bda, sizeof(target));
        mac_to_emit = s_pair_pending.mac;
    } else {
        return ESP_ERR_INVALID_STATE;
    }

    if (!mac_to_emit || mac_to_emit[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    char confirm_payload[64];
    safe_snprintf(confirm_payload, sizeof(confirm_payload), "%s,%s", mac_to_emit, pin);
    cmd_send_event_pair("CONFIRM", confirm_payload);

    esp_err_t mock_err = bt_mock_send_pin(pin);
    if (mock_err == ESP_OK) {
        bt_pairing_clear_pending_flags(true, false);
    }
    return mock_err;
#else
    esp_bd_addr_t target = {0};

    if (mac && mac[0] != '\0') {
        if (!bt_pairing_parse_mac_string(mac, target)) {
            return ESP_ERR_INVALID_ARG;
        }
    } else if (s_pair_pending.pin_pending) {
        safe_memcpy(target, sizeof(target), s_pair_pending.bda, sizeof(target));
    } else {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t pin_code[ESP_BT_PIN_CODE_LEN] = {0};
    size_t pin_len = strlen(pin);
    if (pin_len > ESP_BT_PIN_CODE_LEN) {
        pin_len = ESP_BT_PIN_CODE_LEN;
    }
    safe_memcpy(pin_code, pin_len, pin, pin_len);

    esp_err_t err = esp_bt_gap_pin_reply(target, true, (uint8_t)pin_len, pin_code);
    if (err == ESP_OK) {
        bt_pairing_clear_pending_flags(true, false);
    }
    return err;
#endif
#else
    (void)mac;
    (void)pin;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// C-compatible wrappers expected by other components (command_interface)
// These provide a simple int-based return (0=success) while delegating to
// the bt_manager-style APIs above.
// Manager-prefixed C wrappers that forward to the bt_manager API. These are
// used by other components after we update their forward declarations.
int bt_manager_set_name(const char* name) {
#ifdef ESP_PLATFORM
    if (!bt_ctx.initialized) return -1;
    esp_err_t err = esp_bt_gap_set_device_name(name);
    return (err == ESP_OK) ? 0 : -1;
#else
    (void)name;
    return 0;
#endif
}

int bt_manager_pair(const char* mac) {
    return (bt_pair(mac) == ESP_OK) ? 0 : -1;
}

int bt_manager_connect(const char* mac) {
    return (bt_connect(mac) == ESP_OK) ? 0 : -1;
}

int bt_manager_disconnect(void) {
#ifdef UNIT_TEST
    /* Test hook: allow unit tests to force a failure path. Implemented in
     * test code via a weak symbol returning non-zero when a failure should
     * be simulated. */
    extern int bt_manager_forced_disconnect_failure(void);
    if (bt_manager_forced_disconnect_failure()) return -1;
#endif
    return (bt_disconnect() == ESP_OK) ? 0 : -1;
}

int bt_manager_start_audio(void) {
#ifdef UNIT_TEST
    extern int bt_manager_forced_start_failure(void);
    if (bt_manager_forced_start_failure()) return -1;
#endif
    return (bt_start_audio() == ESP_OK) ? 0 : -1;
}

// Wrapper so command layer and host tests can call a consistent bt_manager_
// prefixed API for starting scans. Returns 0 on success, -1 on failure.
int bt_manager_start_scan(void) {
#ifdef UNIT_TEST
    /* Allow tests to force a failure path if they need to exercise error
     * handling. Tests may provide a weak symbol to indicate a forced
     * failure; absent that symbol the normal path is taken. */
    extern int bt_manager_forced_start_failure(void);
    if (bt_manager_forced_start_failure()) return -1;
#endif
    int ret = (bt_start_scan() == ESP_OK) ? 0 : -1;

#if defined(UNIT_TEST)
    /* In unit-test/host builds, ensure the test hook is invoked when a
     * scan is started. Some host build configs may not execute the
     * internal bt_start_scan() hook due to differing compilation flags;
     * calling the test hook here guarantees the host-mode mock observes
     * the scan-start regardless. The hook is weakly linked in the test
     * harness (`mocks/bt_manager_test_hooks.c`) so this call is a no-op
     * in production builds. */
    if (ret == 0) {
        extern void bt_manager_test_record_scan_start(void);
        /* Call the weak test hook unconditionally — production builds
         * provide a no-op weak definition at file scope, and host tests
         * provide an overriding implementation. Checking the function
         * pointer value triggers -Werror=address on newer GCCs, so
         * avoid that pattern and call directly. */
        bt_manager_test_record_scan_start();
    }
#endif

    return ret;
}

// Wrapper so command layer and host tests can call a consistent bt_manager_
// prefixed API for starting a pairing operation. Returns 0 on success, -1 on
// failure. In unit-test builds this will also invoke a weak test hook so the
// host-side test harness can deterministically observe that pairing was
// initiated.
int bt_manager_start_pair(const char* mac) {
#ifdef UNIT_TEST
    extern int bt_manager_forced_pair_failure(void);
    if (bt_manager_forced_pair_failure()) return -1;
#endif
    /* Call the underlying bt_pair() so we can capture the esp_err_t and
     * emit a diagnostic when pairing fails. bt_manager_pair() hides the
     * exact error code which makes on-target triage harder; using the
     * lower-level API preserves behaviour while improving visibility. */
    bt_err_t perr = bt_pair(mac);
    int ret = (perr == ESP_OK) ? 0 : -1;

#if defined(ESP_PLATFORM)
    if (ret != 0) {
        ESP_LOGE(TAG, "bt_manager_start_pair: failed to start pairing for %s: %s (%d)",
                 mac ? mac : "<null>", esp_err_to_name(perr), (int)perr);
    }
#else
    if (ret != 0) {
        printf("DIAG: bt_manager_start_pair failed for %s: err=%d\n", mac ? mac : "<null>", (int)perr);
        fflush(stdout);
    }
#endif

#if defined(UNIT_TEST)
    if (ret == 0) {
        extern void bt_manager_test_record_pair_start(const char* mac);
        /* Call the weak test hook directly; production builds provide a
         * no-op weak definition and host tests provide an overriding
         * implementation. Avoid taking the function pointer address to
         * prevent -Werror=address warnings on some toolchains. */
        bt_manager_test_record_pair_start(mac);
    }
#endif

    return ret;
}

int bt_manager_stop_audio(void) {
#ifdef UNIT_TEST
    extern int bt_manager_forced_stop_failure(void);
    if (bt_manager_forced_stop_failure()) return -1;
#endif
    return (bt_stop_audio() == ESP_OK) ? 0 : -1;
}


#if defined(ESP_PLATFORM) || defined(UNIT_TEST)
static void bt_manager_handle_a2dp_connection(const esp_a2d_cb_param_t *param) {
    if (!param) {
        return;
    }

    if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        char bda_str[18];
        safe_snprintf(bda_str, sizeof(bda_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                      param->conn_stat.remote_bda[0], param->conn_stat.remote_bda[1],
                      param->conn_stat.remote_bda[2], param->conn_stat.remote_bda[3],
                      param->conn_stat.remote_bda[4], param->conn_stat.remote_bda[5]);

        ESP_LOGI(TAG, "Connected to device: %s", bda_str);

        bt_ctx.connected = true;
        safe_copy_str(bt_ctx.connected_mac, sizeof(bt_ctx.connected_mac), bda_str);

        if (bt_ctx.connected_callback != NULL) {
            bt_ctx.connected_callback(bda_str, bt_ctx.connected_name);
        }

        esp_bd_addr_t tmp_addr = {0};
        safe_memcpy(tmp_addr, sizeof(tmp_addr), param->conn_stat.remote_bda, sizeof(tmp_addr));
        bt_connection_state_cb(param->conn_stat.state, tmp_addr);

        if (s_autostart_enabled) {
    #if defined(UNIT_TEST)
            s_autostart_attempts++;
    #endif
            bt_err_t start_ret = bt_start_audio();
            ESP_LOGI(TAG, "Auto-start after connect -> %s", start_ret == ESP_OK ? "OK" : esp_err_to_name(start_ret));
        }
    } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected from device: %s", bt_ctx.connected_mac);

        if (bt_ctx.disconnected_callback != NULL) {
            bt_ctx.disconnected_callback(bt_ctx.connected_mac);
        }

        bt_ctx.connected = false;
        bt_ctx.audio_playing = false;
        esp_bd_addr_t tmp_addr = {0};
        safe_memcpy(tmp_addr, sizeof(tmp_addr), param->conn_stat.remote_bda, sizeof(tmp_addr));
        bt_connection_state_cb(param->conn_stat.state, tmp_addr);
    }
}

static void bt_manager_handle_a2dp_audio(const esp_a2d_cb_param_t *param) {
    if (!param) {
        return;
    }

    if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
        ESP_LOGI(TAG, "Audio streaming started");
        bt_ctx.audio_playing = true;
        esp_bd_addr_t tmp_addr = {0};
        safe_memcpy(tmp_addr, sizeof(tmp_addr), param->audio_stat.remote_bda, sizeof(tmp_addr));
        bt_audio_state_cb(param->audio_stat.state, tmp_addr);
    } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STOPPED) {
        ESP_LOGI(TAG, "Audio streaming stopped");
        bt_ctx.audio_playing = false;
        esp_bd_addr_t tmp_addr = {0};
        safe_memcpy(tmp_addr, sizeof(tmp_addr), param->audio_stat.remote_bda, sizeof(tmp_addr));
        bt_audio_state_cb(param->audio_stat.state, tmp_addr);
    } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) {
        ESP_LOGI(TAG, "Audio streaming remote suspend");
        bt_ctx.audio_playing = false;
        esp_bd_addr_t tmp_addr = {0};
        safe_memcpy(tmp_addr, sizeof(tmp_addr), param->audio_stat.remote_bda, sizeof(tmp_addr));
        bt_audio_state_cb(param->audio_stat.state, tmp_addr);
    }
}
#endif

#ifdef ESP_PLATFORM
// GAP callback for Bluetooth events
static void bt_app_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            // Device discovery result
            char bda_str[18];
            char name[32] = {0};
            uint32_t cod = 0;
            int8_t rssi = 0;
            
            // Get device address
                 safe_snprintf(bda_str, sizeof(bda_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                         param->disc_res.bda[0], param->disc_res.bda[1], param->disc_res.bda[2],
                         param->disc_res.bda[3], param->disc_res.bda[4], param->disc_res.bda[5]);
            
            // Get device name
            for (int i = 0; i < param->disc_res.num_prop; i++) {
                if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_BDNAME) {
                    safe_memcpy(name, sizeof(name), param->disc_res.prop[i].val, param->disc_res.prop[i].len);
                    name[sizeof(name) - 1] = '\0';
                } else if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_COD) {
                    safe_memcpy(&cod, sizeof(cod), param->disc_res.prop[i].val, sizeof(uint32_t));
                } else if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_RSSI) {
                    safe_memcpy(&rssi, sizeof(rssi), param->disc_res.prop[i].val, sizeof(int8_t));
                }
            }
            
            ESP_LOGI(TAG, "Device found: %s, name: %s, RSSI: %d", bda_str, name, rssi);
            
            // Add to discovered devices list
            if (bt_ctx.discovered_devices.count < 20) {
                int idx = bt_ctx.discovered_devices.count;
                safe_copy_str(bt_ctx.discovered_devices.devices[idx].mac,
                          sizeof(bt_ctx.discovered_devices.devices[idx].mac), bda_str);
                safe_copy_str(bt_ctx.discovered_devices.devices[idx].name,
                          sizeof(bt_ctx.discovered_devices.devices[idx].name), name);
                bt_ctx.discovered_devices.devices[idx].rssi = rssi;
                bt_ctx.discovered_devices.count++;
            }
            break;
        }
        
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            // Discovery state changed
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                ESP_LOGI(TAG, "Device discovery stopped");
                bt_ctx.scanning = false;
            } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
                ESP_LOGI(TAG, "Device discovery started");
                bt_ctx.scanning = true;
            }
            break;
            
        case ESP_BT_GAP_PIN_REQ_EVT: {
            // Remote device is requesting a PIN code (legacy pairing)
            bt_gap_handle_pin_req(param->pin_req.bda);
            break;
        }

        case ESP_BT_GAP_CFM_REQ_EVT: {
            // SSP confirmation request (numeric comparison)
            bt_gap_handle_ssp_confirm(param->cfm_req.bda, param->cfm_req.num_val);
            break;
        }

        case ESP_BT_GAP_AUTH_CMPL_EVT: {
            // Authentication complete (pairing result)
            bt_gap_handle_auth_cmpl(param->auth_cmpl.bda, param->auth_cmpl.stat);
            break;
        }

        default:
            break;
    }
}

// A2DP callback for audio events
static void bt_app_a2d_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            bt_manager_handle_a2dp_connection(param);
            break;
        case ESP_A2D_AUDIO_STATE_EVT:
            bt_manager_handle_a2dp_audio(param);
            break;
        default:
            break;
    }
}

// Minimal AVRCP controller callback: log connection state and ignore others
static void bt_app_avrc_ct_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    if (param == NULL) {
        return;
    }

    switch (event) {
        case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
            /* IDF provides 'connected' bool in conn_stat; avoid accessing
             * non-existent fields on older/newer headers. */
            bool connected = false;
#if defined(ESP_AVRC_CT_CONNECTION_STATE_EVT)
            connected = param->conn_stat.connected;
#endif
            ESP_LOGI(TAG, "AVRCP connection state: %d", connected ? 1 : 0);
            break;
        }
        case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
            ESP_LOGD(TAG, "AVRCP passthrough rsp key=%d state=%d", param->psth_rsp.key_code, param->psth_rsp.key_state);
            break;
        case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
            ESP_LOGD(TAG, "AVRCP remote features: 0x%x", (unsigned int)param->rmt_feats.feat_mask);
            break;
        default:
            ESP_LOGD(TAG, "AVRCP event: %d", event);
            break;
    }
}

static esp_err_t bt_manager_init_profiles(void)
{
    esp_err_t ret = esp_avrc_ct_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialize AVRCP controller failed: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    ret = esp_avrc_ct_register_callback(bt_app_avrc_ct_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register AVRCP controller callback failed: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    ret = esp_a2d_source_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialize a2dp source failed: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    ret = esp_a2d_register_callback(bt_app_a2d_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register a2dp source callback failed: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    ret = esp_a2d_source_register_data_callback(bt_app_a2d_data_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register a2dp data callback failed: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Updated to match esp_a2d_source_data_cb_t: fill buffer and return bytes written
static int32_t bt_app_a2d_data_callback(uint8_t *buf, int32_t len) {
    if (len <= 0 || buf == NULL) {
        return 0;
    }

    if (s_trace_read_until_beep_done) {
        size_t queue_used = audio_descriptor_used();
        size_t queue_free = audio_processor_queue_free_bytes();
         printf("TRACE-A2DP-CB: req=%ld queue_used=%zu queue_free=%zu beep_remaining=%zu\n",
             (long)len,
               queue_used,
               queue_free,
               s_beep_remaining_bytes);
    }

    static audio_chunk_t s_pending = {0};
    static size_t s_pending_offset = 0;
    static bool s_pending_valid = false;

    size_t produced = 0;
    while (produced < (size_t)len) {
        audio_chunk_t *chunk = NULL;
        if (s_pending_valid) {
            chunk = &s_pending;
        } else {
            audio_chunk_t fresh = {0};
            esp_err_t acq = audio_processor_acquire_chunk(&fresh, 0);
            if (acq != ESP_OK) {
                break;
            }
            s_pending = fresh;
            s_pending_offset = 0;
            s_pending_valid = true;
            chunk = &s_pending;
        }

        size_t available = (chunk->len > s_pending_offset) ? (chunk->len - s_pending_offset) : 0;
        if (available == 0) {
            audio_processor_release_chunk(chunk);
            s_pending_valid = false;
            s_pending_offset = 0;
            continue;
        }

        size_t to_copy = available;
        if (to_copy > ((size_t)len - produced)) {
            to_copy = (size_t)len - produced;
        }

        util_safe_memcpy(buf + produced, (size_t)len - produced, chunk->data + s_pending_offset, to_copy);
        produced += to_copy;
        s_pending_offset += to_copy;

        if (s_pending_offset >= chunk->len) {
            audio_processor_release_chunk(chunk);
            s_pending_valid = false;
            s_pending_offset = 0;
        }
    }

    if (produced == 0) {
        // Fallback: provide silence if no data available
        safe_memset(buf, 0, (size_t)len);
        return len;
    }

    if (s_trace_read_until_beep_done) {
        printf("TRACE-A2DP-CB-RET: produced=%zu\n", produced);
    }

    return (int32_t)produced;
}
#endif

// For testing only - not for production use
#ifdef UNIT_TEST
void bt_manager_mock_device_found(const bt_device_t* device) {
    if (!bt_ctx.initialized || !bt_ctx.scanning || device == NULL) {
        return;
    }
    
    int idx = bt_ctx.discovered_devices.count;
    if (idx < 20) {
        safe_memcpy(&bt_ctx.discovered_devices.devices[idx], sizeof(bt_ctx.discovered_devices.devices[idx]), device, sizeof(bt_device_t));
        bt_ctx.discovered_devices.count++;
        
        // Log the device found
        printf("Mock BT: Device found: %s, name: %s, RSSI: %d\n", 
               device->mac, device->name, device->rssi);
    }
}

void bt_manager_mock_connection_established(const char* mac, const char* name) {
    printf("TRACE: bt_manager_mock_connection_established called\n");
    if (!bt_ctx.initialized) {
        /*
         * Unit tests often call the mock connection helpers without
         * first calling the full manager init sequence. Make the mock
         * helper tolerant by forcing the manager into an initialized
         * state so subsequent state changes and callbacks behave as
         * tests expect.
         */
        bt_ctx.initialized = true;
    }
    
    bt_ctx.connected = true;
    safe_copy_str(bt_ctx.connected_mac, sizeof(bt_ctx.connected_mac), mac);
    safe_copy_str(bt_ctx.connected_name, sizeof(bt_ctx.connected_name), name);
    
    // Log the connection
    printf("Mock BT: Connected to device: %s, name: %s\n", mac, name);
    
    if (bt_ctx.connected_callback != NULL) {
        bt_ctx.connected_callback(mac, name);
    }

    /* Test hook: when unit tests are built, call into a test-provided
     * setter so host-mode mocks can keep a test-visible connection flag in
     * step with bt_ctx.connected. The symbol is optional and only present
     * in unit-test builds where the test shim provides it. */
#ifdef UNIT_TEST
    extern void bt_manager_test_set_connection_state(int);
    bt_manager_test_set_connection_state(1);
#endif

}

void bt_manager_mock_connection_closed(const char* mac) {
    if (!bt_ctx.initialized) {
        /* Ensure the context exists for the mock close operation so the
         * function can consistently clear connection state even if the
         * test harness didn't call bt_manager_force_initialized(). */
        bt_ctx.initialized = true;
    }
    if (!bt_ctx.connected) {
        return;
    }
    
    // Log the disconnection
    printf("Mock BT: Disconnected from device: %s\n", mac);
    
    bt_ctx.connected = false;
    bt_ctx.audio_playing = false;
    
    if (bt_ctx.disconnected_callback != NULL) {
        bt_ctx.disconnected_callback(mac);
    }

#ifdef UNIT_TEST
    extern void bt_manager_test_set_connection_state(int);
    bt_manager_test_set_connection_state(0);
#endif

}

void bt_manager_mock_audio_state_changed(int state) {
    if (!bt_ctx.initialized) {
        return;
    }
    
    bt_ctx.audio_playing = (state == 2); // 2 = ESP_A2D_AUDIO_STATE_STARTED
    
    // Log the audio state change
    printf("Mock BT: Audio state changed to: %s\n", bt_ctx.audio_playing ? "playing" : "stopped");
}

void bt_manager_mock_pairing_complete(const char* mac, bool success) {
    if (!bt_ctx.initialized) {
        return;
    }
    
    // Log the pairing result
    printf("Mock BT: Pairing %s for device: %s\n", success ? "successful" : "failed", mac);
    
    if (success && mac != NULL) {
        // Add device to paired devices
        int idx = bt_ctx.paired_devices.count;
        if (idx < 20) {
                safe_copy_str(bt_ctx.paired_devices.devices[idx].mac,
                      sizeof(bt_ctx.paired_devices.devices[idx].mac), mac);
            
            // In a real scenario, we would have the name, here we just set a placeholder
                safe_snprintf(bt_ctx.paired_devices.devices[idx].name,
                              sizeof(bt_ctx.paired_devices.devices[idx].name),
                              "Device_%s", mac);
            
            bt_ctx.paired_devices.count++;
        }

        /* Keep the mock NVS storage in sync with the in-memory paired list on
         * host builds so subsequent bt_unpair() calls find the entry. */
        nvs_storage_add_paired_device(mac, NULL);
    }
}
#endif

#ifdef UNIT_TEST
// Expose a function to force-initialize bt_ctx for unit tests
void bt_manager_force_initialized(bool value) {
    bt_ctx.initialized = value;
}

// Debug print for test state
void bt_manager_debug_print(void) {
    printf("DEBUG: bt_ctx.initialized=%d, connected=%d, audio_playing=%d\n",
        bt_ctx.initialized, bt_ctx.connected, bt_ctx.audio_playing);
}

bool bt_manager_test_is_audio_playing(void) {
    return bt_ctx.audio_playing;
}
/* Provide a weak default for the optional forced-pair failure hook so
 * unit-test builds that don't need this behavior don't have to provide
 * their own definition. Tests which want to simulate a forced failure
 * may provide a strong symbol overriding this. */
__attribute__((weak)) int bt_manager_forced_pair_failure(void) { return 0; }

/* Unit-test helper: simulate the auto-start path executed after a
 * successful connection. Returns true if bt_start_audio() was invoked
 * by this helper. This lets host tests verify autostart is skipped
 * when audio is already playing or when the feature is disabled. */
bool bt_manager_test_autostart_on_connect(void) {
    if (!bt_ctx.initialized) {
        return false;
    }
    if (!bt_ctx.connected) {
        bt_ctx.connected = true;
    }
    if (!s_autostart_enabled) {
        return false;
    }
    if (bt_ctx.audio_playing) {
        return false;
    }

    return bt_start_audio() == ESP_OK;
}

/* Unit-test helper: reuse the production A2DP handlers so host tests can
 * simulate connection/audio state events and validate autostart/forwarding
 * behaviour without ESP_PLATFORM callbacks. */
#ifdef CONFIG_BT_MOCK_TESTING
/* Optional mock-side hook to keep device test streaming state in sync. */
void bt_source_mock_handle_audio_state(esp_a2d_audio_state_t state) __attribute__((weak));
#endif
void bt_manager_test_invoke_a2dp_event(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
    if (!param) {
        return;
    }

    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            bt_manager_handle_a2dp_connection(param);
            break;
        case ESP_A2D_AUDIO_STATE_EVT:
            bt_manager_handle_a2dp_audio(param);
#ifdef CONFIG_BT_MOCK_TESTING
            if (bt_source_mock_handle_audio_state) {
                bt_source_mock_handle_audio_state(param->audio_stat.state);
            }
#endif
            break;
        default:
            break;
    }
}
#endif

#ifdef UNIT_TEST
esp_err_t bt_manager_test_init_profiles(void)
{
#ifdef ESP_PLATFORM
    return bt_manager_init_profiles();
#else
    /* Host test build: call mock functions in the same order as ESP build */
    esp_err_t ret;
    
    ret = esp_avrc_ct_init();
    if (ret != ESP_OK) {
        return ESP_FAIL;
    }
    
    ret = esp_avrc_ct_register_callback(NULL);
    if (ret != ESP_OK) {
        return ESP_FAIL;
    }
    
    ret = esp_a2d_source_init();
    if (ret != ESP_OK) {
        return ESP_FAIL;
    }
    
    ret = esp_a2d_register_callback(NULL);
    if (ret != ESP_OK) {
        return ESP_FAIL;
    }
    
    ret = esp_a2d_source_register_data_callback(NULL);
    if (ret != ESP_OK) {
        return ESP_FAIL;
    }
    
    return ESP_OK;
#endif
}
#endif

#ifdef ESP_PLATFORM
/* ESP-IDF build */
#else
/* Host build */
#endif
