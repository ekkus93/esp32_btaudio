#include "bt_manager.h"
#include "bt_api.h"
#include "nvs_storage.h"
#include "esp_bt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma message("COMPILING bt_manager.c")

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
#define TAG "BT_MGR"
#endif

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

typedef struct {
    bool pin_pending;
    bool ssp_pending;
    esp_bd_addr_t bda;
    char mac[18];
    uint32_t passkey;
} bt_pairing_pending_t;

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
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    out[out_len - 1] = '\0';
}

static void bt_pairing_set_pending_addr(const esp_bd_addr_t bda)
{
    memcpy(s_pair_pending.bda, bda, sizeof(esp_bd_addr_t));
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
        memset(s_pair_pending.bda, 0, sizeof(s_pair_pending.bda));
        s_pair_pending.mac[0] = '\0';
        s_pair_pending.passkey = 0;
    }
}

static bool bt_pairing_parse_mac_string(const char* mac, esp_bd_addr_t out)
{
    if (!mac || !out) {
        return false;
    }
    unsigned int b0, b1, b2, b3, b4, b5;
    if (sscanf(mac, "%02x:%02x:%02x:%02x:%02x:%02x", &b0, &b1, &b2, &b3, &b4, &b5) != 6) {
        return false;
    }
    out[0] = (uint8_t)b0;
    out[1] = (uint8_t)b1;
    out[2] = (uint8_t)b2;
    out[3] = (uint8_t)b3;
    out[4] = (uint8_t)b4;
    out[5] = (uint8_t)b5;
    return true;
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

#ifdef ESP_PLATFORM
#include <inttypes.h>
#include "nvs_flash.h"
// Callback declarations
static void bt_app_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
static void bt_app_a2d_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);
// esp_a2d_source_data_cb_t signature: int32_t (*)(uint8_t *buf, int32_t len)
static int32_t bt_app_a2d_data_callback(uint8_t *buf, int32_t len);
#include "command_interface.h"
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
#endif

#ifdef UNIT_TEST
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
    bt_pairing_prepare_pending_for_event(addr);
    s_pair_pending.pin_pending = true;
    s_pair_pending.ssp_pending = false;
    s_pair_pending.passkey = 0;
    return true;
}

bool bt_manager_test_gap_ssp_confirm(const char* mac, uint32_t passkey)
{
    esp_bd_addr_t addr = {0};
    if (!bt_pairing_parse_mac_string(mac, addr)) {
        return false;
    }
    bt_pairing_prepare_pending_for_event(addr);
    s_pair_pending.pin_pending = false;
    s_pair_pending.ssp_pending = true;
    s_pair_pending.passkey = passkey;
    return true;
}

void bt_manager_test_gap_auth_complete(const char* mac, bool success)
{
    (void)success;
    esp_bd_addr_t addr = {0};
    if (!bt_pairing_parse_mac_string(mac, addr)) {
        return;
    }
    bt_pairing_prepare_pending_for_event(addr);
    bt_pairing_clear_pending_flags(true, true);
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
    
    // Store configuration
    strncpy(bt_ctx.device_name, config->device_name, sizeof(bt_ctx.device_name) - 1);
    bt_ctx.connected_callback = config->connected_cb;
    bt_ctx.disconnected_callback = config->disconnected_cb;
    
    // Initialize structures
    memset(&bt_ctx.discovered_devices, 0, sizeof(bt_ctx.discovered_devices));
    memset(&bt_ctx.paired_devices, 0, sizeof(bt_ctx.paired_devices));
    
#ifdef ESP_PLATFORM
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize Bluetooth controller
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

    // Initialize A2DP source
    if ((ret = esp_a2d_source_init()) != ESP_OK) {
        ESP_LOGE(TAG, "Initialize a2dp source failed: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    // Register A2DP source callback
    if ((ret = esp_a2d_register_callback(bt_app_a2d_callback)) != ESP_OK) {
        ESP_LOGE(TAG, "Register a2dp source callback failed: %s", esp_err_to_name(ret));
    return ESP_FAIL;
    }

    // Register data callback
    if ((ret = esp_a2d_source_register_data_callback(bt_app_a2d_data_callback)) != ESP_OK) {
        ESP_LOGE(TAG, "Register a2dp data callback failed: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    // Set device discoverable and connectable
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    
    ESP_LOGI(TAG, "Bluetooth manager initialized with name: %s", config->device_name);
    
    // Initialize app NVS wrappers and load persisted paired devices
    if (nvs_storage_init() == ESP_OK) {
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
                    strncpy(bt_ctx.paired_devices.devices[idx].mac, mac, sizeof(bt_ctx.paired_devices.devices[idx].mac)-1);
                    if (name[0]) strncpy(bt_ctx.paired_devices.devices[idx].name, name, sizeof(bt_ctx.paired_devices.devices[idx].name)-1);
                    bt_ctx.paired_devices.count++;
                }
            }
            ESP_LOGI(TAG, "Loaded %d persisted paired devices", bt_ctx.paired_devices.count);
        }
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

// Start device scanning
 bt_err_t bt_start_scan(void) {
    if (!bt_ctx.initialized) {
        return ESP_FAIL;
    }
    
    if (bt_ctx.scanning) {
        return ESP_OK; // Already scanning
    }
    
    // Clear previous discovered devices
    memset(&bt_ctx.discovered_devices, 0, sizeof(bt_ctx.discovered_devices));
    
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
    printf("TRACE: bt_connect called\n");
    if (!bt_ctx.initialized) {
        printf("TRACE: bt_connect - bt_connect not initialized\n");
        fflush(stdout);
        return ESP_FAIL;
    }
    
    if (mac == NULL) {
        printf("TRACE: bt_connect - bt_connect invalid MAC\n");
        fflush(stdout);
        return ESP_FAIL;
    }
    
    if (bt_ctx.connected) {
        printf("TRACE: bt_connect - bt_connect already connected\n");
        fflush(stdout);
        return ESP_FAIL;
    }
    
#ifdef ESP_PLATFORM
    // Convert MAC string to address
    esp_bd_addr_t bda;
    if (sscanf(mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", 
               &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) != 6) {
        printf("TRACE: bt_connect invalid MAC format\n");
        fflush(stdout);
        ESP_LOGE(TAG, "Invalid MAC address format: %s", mac);
    return ESP_FAIL;
    }
    
    // Connect to device
    if (esp_a2d_source_connect(bda) != ESP_OK) {
        printf("TRACE: bt_connect failed to connect\n");
        fflush(stdout);
        ESP_LOGE(TAG, "Failed to connect to device: %s", mac);
    return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Connecting to device: %s", mac);
    strncpy(bt_ctx.connected_mac, mac, sizeof(bt_ctx.connected_mac) - 1);
#endif
    printf("TRACE: bt_connect success\n");
    fflush(stdout);
    
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
    if (sscanf(bt_ctx.connected_mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", 
               &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) != 6) {
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

    memset(&bt_ctx.paired_devices, 0, sizeof(bt_ctx.paired_devices));

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
    memcpy(pin_code, pin, pin_len);
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
        memset(info, 0, sizeof(*info));
        return false;
    }
    info->pin_request_pending = s_pair_pending.pin_pending;
    info->ssp_confirm_pending = s_pair_pending.ssp_pending;
    strncpy(info->mac, s_pair_pending.mac, sizeof(info->mac) - 1);
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
        memcpy(target, s_pair_pending.bda, sizeof(target));
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
        memcpy(target, s_pair_pending.bda, sizeof(target));
        mac_to_emit = s_pair_pending.mac;
    } else {
        return ESP_ERR_INVALID_STATE;
    }

    if (!mac_to_emit || mac_to_emit[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    char confirm_payload[64];
    snprintf(confirm_payload, sizeof(confirm_payload), "%s,%s", mac_to_emit, pin);
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
        memcpy(target, s_pair_pending.bda, sizeof(target));
    } else {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t pin_code[ESP_BT_PIN_CODE_LEN] = {0};
    size_t pin_len = strlen(pin);
    if (pin_len > ESP_BT_PIN_CODE_LEN) {
        pin_len = ESP_BT_PIN_CODE_LEN;
    }
    memcpy(pin_code, pin, pin_len);

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

int bt_manager_stop_audio(void) {
#ifdef UNIT_TEST
    extern int bt_manager_forced_stop_failure(void);
    if (bt_manager_forced_stop_failure()) return -1;
#endif
    return (bt_stop_audio() == ESP_OK) ? 0 : -1;
}


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
            sprintf(bda_str, "%02x:%02x:%02x:%02x:%02x:%02x",
                   param->disc_res.bda[0], param->disc_res.bda[1], param->disc_res.bda[2],
                   param->disc_res.bda[3], param->disc_res.bda[4], param->disc_res.bda[5]);
            
            // Get device name
            for (int i = 0; i < param->disc_res.num_prop; i++) {
                if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_BDNAME) {
                    memcpy(name, param->disc_res.prop[i].val, param->disc_res.prop[i].len);
                    name[param->disc_res.prop[i].len] = '\0';
                } else if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_COD) {
                    memcpy(&cod, param->disc_res.prop[i].val, sizeof(uint32_t));
                } else if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_RSSI) {
                    memcpy(&rssi, param->disc_res.prop[i].val, sizeof(int8_t));
                }
            }
            
            ESP_LOGI(TAG, "Device found: %s, name: %s, RSSI: %d", bda_str, name, rssi);
            
            // Add to discovered devices list
            if (bt_ctx.discovered_devices.count < 20) {
                int idx = bt_ctx.discovered_devices.count;
                strncpy(bt_ctx.discovered_devices.devices[idx].mac, bda_str, 
                        sizeof(bt_ctx.discovered_devices.devices[idx].mac) - 1);
                strncpy(bt_ctx.discovered_devices.devices[idx].name, name, 
                        sizeof(bt_ctx.discovered_devices.devices[idx].name) - 1);
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
            char bda_str[18];
            sprintf(bda_str, "%02x:%02x:%02x:%02x:%02x:%02x",
                   param->pin_req.bda[0], param->pin_req.bda[1], param->pin_req.bda[2],
                   param->pin_req.bda[3], param->pin_req.bda[4], param->pin_req.bda[5]);

            ESP_LOGI(TAG, "PIN request from device: %s", bda_str);
            bt_pairing_prepare_pending_for_event(param->pin_req.bda);
            s_pair_pending.pin_pending = true;
            s_pair_pending.passkey = 0;
            // Notify command interface: EVENT|PAIR|PIN_REQUEST|<MAC>
            cmd_send_event_pair("PIN_REQUEST", bda_str);
            break;
        }

        case ESP_BT_GAP_CFM_REQ_EVT: {
            // SSP confirmation request (numeric comparison)
            char bda_str[18];
            sprintf(bda_str, "%02x:%02x:%02x:%02x:%02x:%02x",
                   param->cfm_req.bda[0], param->cfm_req.bda[1], param->cfm_req.bda[2],
                   param->cfm_req.bda[3], param->cfm_req.bda[4], param->cfm_req.bda[5]);

            // num_val contains the numeric comparison value (32-bit)
            char data[64];
            snprintf(data, sizeof(data), "%s,%u", bda_str, (unsigned int)param->cfm_req.num_val);
            ESP_LOGI(TAG, "SSP confirm request from %s value=%u", bda_str, (unsigned int)param->cfm_req.num_val);
            bt_pairing_prepare_pending_for_event(param->cfm_req.bda);
            s_pair_pending.ssp_pending = true;
            s_pair_pending.passkey = param->cfm_req.num_val;
            // Notify command interface: EVENT|PAIR|CONFIRM|<MAC>,<NUM>
            cmd_send_event_pair("CONFIRM", data);
            break;
        }

        case ESP_BT_GAP_AUTH_CMPL_EVT: {
            // Authentication complete (pairing result)
            char bda_str[18] = {0};
            /* bda is an array member (not a pointer) - format directly */
            sprintf(bda_str, "%02x:%02x:%02x:%02x:%02x:%02x",
                   param->auth_cmpl.bda[0], param->auth_cmpl.bda[1], param->auth_cmpl.bda[2],
                   param->auth_cmpl.bda[3], param->auth_cmpl.bda[4], param->auth_cmpl.bda[5]);
            bt_pairing_prepare_pending_for_event(param->auth_cmpl.bda);
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Authentication (pairing) successful: %s", bda_str);
                cmd_send_event_pair("SUCCESS", bda_str);
                // Persist paired device
                // Try to get a device name from discovered list if available
                char dev_name[32] = {0};
                for (int i = 0; i < bt_ctx.discovered_devices.count; i++) {
                    if (strcmp(bt_ctx.discovered_devices.devices[i].mac, bda_str) == 0) {
                        strncpy(dev_name, bt_ctx.discovered_devices.devices[i].name, sizeof(dev_name)-1);
                        break;
                    }
                }
                nvs_storage_add_paired_device(bda_str, dev_name[0] ? dev_name : NULL);
            } else {
                ESP_LOGW(TAG, "Authentication (pairing) failed: %s", bda_str);
                cmd_send_event_pair("FAILED", bda_str);
            }
            bt_pairing_clear_pending_flags(true, true);
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
            // Connection state changed
            if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                char bda_str[18];
                sprintf(bda_str, "%02x:%02x:%02x:%02x:%02x:%02x",
                       param->conn_stat.remote_bda[0], param->conn_stat.remote_bda[1],
                       param->conn_stat.remote_bda[2], param->conn_stat.remote_bda[3],
                       param->conn_stat.remote_bda[4], param->conn_stat.remote_bda[5]);
                
                ESP_LOGI(TAG, "Connected to device: %s", bda_str);
                
                bt_ctx.connected = true;
                strncpy(bt_ctx.connected_mac, bda_str, sizeof(bt_ctx.connected_mac) - 1);
                
                // Get device name (simplified, actual implementation would get name from bt_ctx.discovered_devices)
                if (bt_ctx.connected_callback != NULL) {
                    bt_ctx.connected_callback(bda_str, bt_ctx.connected_name);
                }
            } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                ESP_LOGI(TAG, "Disconnected from device: %s", bt_ctx.connected_mac);
                
                if (bt_ctx.disconnected_callback != NULL) {
                    bt_ctx.disconnected_callback(bt_ctx.connected_mac);
                }
                
                bt_ctx.connected = false;
                bt_ctx.audio_playing = false;
            }
            break;
            
        case ESP_A2D_AUDIO_STATE_EVT:
            // Audio state changed
            if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
                ESP_LOGI(TAG, "Audio streaming started");
                bt_ctx.audio_playing = true;
            } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STOPPED) {
                ESP_LOGI(TAG, "Audio streaming stopped");
                bt_ctx.audio_playing = false;
            }
            break;
            
        // Add other A2DP event handlers as needed
        default:
            break;
    }
}

// Updated to match esp_a2d_source_data_cb_t: fill buffer and return bytes written
static int32_t bt_app_a2d_data_callback(uint8_t *buf, int32_t len) {
    if (len <= 0 || buf == NULL) {
        return 0;
    }
    // Attempt to pull data from the audio processor so A2DP sends live audio.
    size_t bytes_read = 0;
    if (audio_processor_read(buf, (size_t)len, &bytes_read) == ESP_OK && bytes_read > 0) {
        return (int32_t)bytes_read;
    }

    // Fallback: provide silence if no data available
    memset(buf, 0, len);
    return len;
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
        memcpy(&bt_ctx.discovered_devices.devices[idx], device, sizeof(bt_device_t));
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
    strncpy(bt_ctx.connected_mac, mac, sizeof(bt_ctx.connected_mac) - 1);
    strncpy(bt_ctx.connected_name, name, sizeof(bt_ctx.connected_name) - 1);
    
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
            strncpy(bt_ctx.paired_devices.devices[idx].mac, mac, 
                    sizeof(bt_ctx.paired_devices.devices[idx].mac) - 1);
            
            // In a real scenario, we would have the name, here we just set a placeholder
            snprintf(bt_ctx.paired_devices.devices[idx].name, 
                     sizeof(bt_ctx.paired_devices.devices[idx].name) - 1, 
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
#endif

#ifdef ESP_PLATFORM
#pragma message("ESP_PLATFORM is defined")
#else
#pragma message("ESP_PLATFORM is NOT defined")
#endif
