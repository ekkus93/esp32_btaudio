#include "bt_manager.h"
#include "bt_manager_internal.h"
/* CODE_REVIEW8 P2: Need bt_manager_status_t but skip duplicate bt_device_t */
#define BT_SOURCE_SKIP_DEVICE_STRUCT 1
#include "bt_source.h"      /* For bt_manager_status_t (CODE_REVIEW8 P2) */
#undef BT_SOURCE_SKIP_DEVICE_STRUCT
#include "bt_app_core.h"    /* For bt_app_send_mgr_request() (CODE_REVIEW8 P2) */
#include "bt_api.h"
#include "bt_pairing_store.h"
#include "bt_scan.h"
#include "bt_connection.h"
#include "bt_events_gap.h"
#include "bt_events_a2dp.h"
#include "bt_events_avrc.h"
#include "nvs_storage.h"
#include "esp_bt.h"
#include "util_safe.h"
#include "audio_processor.h"
#include "platform_sync.h"  /* CODE_REVIEW8 P2.2 Phase 1: Platform shim for sync */
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
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#define TAG "BT_MGR"
#endif

/*
 * Weak test hooks used by host tests to inject failures. Production builds
 * fall back to these no-op implementations so linking succeeds when the test
 * overrides are absent.
 */
MAYBE_WEAK int bt_manager_forced_disconnect_failure(void) { return 0; }
MAYBE_WEAK int bt_manager_forced_start_failure(void) { return 0; }
MAYBE_WEAK int bt_manager_forced_stop_failure(void) { return 0; }

// Private data
bt_manager_context_t bt_ctx = {
    .initialized = false,
    .scanning = false,
    .connected = false,
    .audio_playing = false,
    .volume = 75
};

/* Runtime preference: automatically issue START once connected/when audio is
 * requested so the sink begins draining without manual intervention. */
bool s_autostart_enabled = true;

#define safe_vsnprintf util_safe_vsnprintf
#define safe_snprintf util_safe_snprintf
#define safe_copy_str util_safe_copy_str
#define safe_memcpy util_safe_memcpy
#define safe_memset util_safe_memset
#define parse_mac_bytes util_parse_mac

#if CONFIG_BT_MOCK_TESTING
#define BT_SOURCE_SKIP_DEVICE_STRUCT 1
#include "bt_source.h"
#undef BT_SOURCE_SKIP_DEVICE_STRUCT
/* Forward declarations from the Bluetooth abstraction so we can delegate
 * pairing to the mock implementation when mock testing configuration is
 * enabled. These symbols live in the bt_mock component. */
esp_err_t bt_start_pairing(const char* addr);
bt_pairing_method_t bt_mock_get_pairing_method(void);
esp_err_t bt_mock_get_ssp_passkey(char* passkey, size_t size);
#endif

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
static esp_err_t bt_manager_init_profiles(void);
// Audio processor API - used by A2DP data callback to pull PCM
#include "audio_processor.h"
#endif

#ifdef UNIT_TEST
MAYBE_WEAK void bt_manager_test_record_unpair(const char* mac) {
    (void)mac;
}

MAYBE_WEAK int bt_manager_test_should_force_unpair_failure(void) { return 0; }

MAYBE_WEAK int bt_manager_test_should_force_unpair_all_failure(void) { return 0; }

MAYBE_WEAK void bt_manager_test_record_unpair_all_call(int cleared_before, int removed) {
    (void)cleared_before;
    (void)removed;
}

MAYBE_WEAK void bt_manager_test_record_unpair_all_temp_alloc(int delta) {
    (void)delta;
}

// Unit-test hook: record when a pairing attempt is started. Provided by host mocks.
MAYBE_WEAK void bt_manager_test_record_pair_start(const char* mac) { (void)mac; }

#ifdef UNIT_TEST
/* Unit test tracking for auto-start attempts - accessed by event handlers */
int s_autostart_attempts = 0;
#endif

int bt_manager_test_get_autostart_attempts(void) {
    return s_autostart_attempts;
}

void bt_manager_test_reset_autostart_attempts(void) {
    s_autostart_attempts = 0;
}

void bt_manager_test_reset_pending(void)
{
    bt_pairing_clear_pending();
}

bool bt_manager_test_gap_pin_request(const char* mac)
{
    esp_bd_addr_t addr = {0};
    if (!bt_pairing_parse_mac(mac, addr)) {
        return false;
    }
    bt_pairing_handle_pin_request(addr);
    return true;
}

bool bt_manager_test_gap_ssp_confirm(const char* mac, uint32_t passkey)
{
    esp_bd_addr_t addr = {0};
    if (!bt_pairing_parse_mac(mac, addr)) {
        return false;
    }
    bt_pairing_handle_ssp_confirm(addr, passkey);
    return true;
}

void bt_manager_test_gap_auth_complete(const char* mac, bool success)
{
    esp_bd_addr_t addr = {0};
    if (!bt_pairing_parse_mac(mac, addr)) {
        return;
    }
    bt_pairing_handle_auth_complete(addr, success ? ESP_BT_STATUS_SUCCESS : ESP_BT_STATUS_AUTH_FAILURE);
}
#endif

/* ============================================================================
 * BT Manager Request/Response API & Handlers (CODE_REVIEW8 P2)
 * 
 * PUBLIC API: bt_manager_get_status() - Call from ANY task to get BT state
 * PATTERN: Request → BtAppTask queue → Response → Semaphore signal
 * 
 * PURPOSE: Thread-safe access to bt_ctx from cmd_proc task.
 * WHY: bt_ctx updated from BtAppTask (BT events), read from cmd_proc (STATUS).
 *      Without synchronization, cmd_proc can see torn/inconsistent state.
 * 
 * See: code_review/BT_STATE_ACCESS_CONTRACT.md for full design.
 * ============================================================================ */

#ifdef ESP_PLATFORM
/**
 * @brief Get BT manager status from ANY task (thread-safe via queue)
 * 
 * This is the PUBLIC API that command handlers and other components should call
 * to read BT manager state. It posts a request to BtAppTask and waits for the
 * response, ensuring a consistent state snapshot.
 * 
 * @param[out] status Pointer to response structure (filled by BtAppTask)
 * @return ESP_OK on success, ESP_FAIL on timeout or queue error
 * @note Blocks up to 100ms waiting for response. Safe to call from any  task.
 */
esp_err_t bt_manager_get_status(bt_manager_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Create semaphore for request/response synchronization */
    platform_binary_sem_t done_sem = platform_binary_sem_create();
    if (!done_sem) {
        ESP_LOGE(TAG, "Failed to create semaphore for status request");
        return ESP_ERR_NO_MEM;
    }
    
    /* Internal response buffer (matches bt_mgr_status_response_t layout) */
    bt_mgr_status_response_t internal_resp;
    
    /* Build request */
    bt_mgr_request_t req = {
        .type = BT_MGR_REQUEST_GET_STATUS,
        .response_buf = &internal_resp,
        .response_size = sizeof(internal_resp),
        .done_sem = done_sem
    };
    
    /* Post request to BtAppTask */
    if (!bt_app_send_mgr_request(&req)) {
        platform_binary_sem_delete(done_sem);
        ESP_LOGE(TAG, "Failed to post status request to BtAppTask");
        return ESP_FAIL;
    }
    
    /* Wait for response (100ms timeout to prevent deadlock if BtAppTask dies) */
    if (platform_binary_sem_take(done_sem, 100) != ESP_OK) {
        platform_binary_sem_delete(done_sem);
        ESP_LOGE(TAG, "Timeout waiting for status response from BtAppTask");
        return ESP_ERR_TIMEOUT;
    }
    
    platform_binary_sem_delete(done_sem);
    
    /* Copy internal response to public API structure */
    status->initialized = internal_resp.initialized;
    status->connected = internal_resp.connected;
    status->audio_playing = internal_resp.audio_playing;
    status->scanning = internal_resp.scanning;
    safe_copy_str(status->connected_mac, sizeof(status->connected_mac), internal_resp.connected_mac);
    safe_copy_str(status->connected_name, sizeof(status->connected_name), internal_resp.connected_name);
    
    return ESP_OK;
}

/**
 * @brief Handle GET_STATUS request - copy consistent snapshot of bt_ctx
 * 
 * CONTEXT: Called from BtAppTask (safe to read bt_ctx without lock)
 * THREAD-SAFETY: Serial execution via BtAppTask queue ensures consistency
 */
static void bt_mgr_handle_get_status(bt_mgr_request_t *req)
{
    if (!req || !req->response_buf) {
        ESP_LOGE(TAG, "Invalid GET_STATUS request (NULL pointers)");
        if (req && req->done_sem) {
            platform_binary_sem_give(req->done_sem);
        }
        return;
    }

    if (req->response_size < sizeof(bt_mgr_status_response_t)) {
        ESP_LOGE(TAG, "GET_STATUS response buffer too small (%zu < %zu)",
                 req->response_size, sizeof(bt_mgr_status_response_t));
        if (req->done_sem) {
            platform_binary_sem_give(req->done_sem);
        }
        return;
    }

    bt_mgr_status_response_t *resp = (bt_mgr_status_response_t *)req->response_buf;

    /* Safe to read bt_ctx - we're in BtAppTask context with serial execution */
    resp->initialized = bt_ctx.initialized;
    resp->connected = bt_ctx.connected;
    resp->audio_playing = bt_ctx.audio_playing;
    resp->scanning = bt_ctx.scanning;
    
    /* String copies - safe because we have exclusive access in BtAppTask */
    safe_copy_str(resp->connected_mac, sizeof(resp->connected_mac), 
                  bt_ctx.connected_mac);
    safe_copy_str(resp->connected_name, sizeof(resp->connected_name),
                  bt_ctx.connected_name);

    /* Signal completion */
    if (req->done_sem) {
        platform_binary_sem_give(req->done_sem);
    }
}

/**
 * @brief Dispatcher for BT manager state requests
 * 
 * INTEGRATION: Called via bt_app_work_dispatch() with BT_APP_SIG_MGR_REQUEST
 * CONTEXT: Executes in BtAppTask (priority 10)
 */
void bt_mgr_request_handler(uint16_t event, void *param)
{
    bt_mgr_request_t *req = (bt_mgr_request_t *)param;
    
    if (!req) {
        ESP_LOGE(TAG, "bt_mgr_request_handler: NULL request");
        return;
    }

    switch (req->type) {
    case BT_MGR_REQUEST_GET_STATUS:
        bt_mgr_handle_get_status(req);
        break;

    case BT_MGR_REQUEST_GET_STREAMING_INFO:
        /* TODO: Implement in Phase 3 when converting streaming info reads */
        ESP_LOGW(TAG, "GET_STREAMING_INFO not yet implemented");
        if (req->done_sem) {
            platform_binary_sem_give(req->done_sem);  /* Signal to prevent deadlock */
        }
        break;

    default:
        ESP_LOGE(TAG, "Unknown request type: %d", req->type);
        if (req->done_sem) {
            platform_binary_sem_give(req->done_sem);
        }
        break;
    }
}
#endif /* ESP_PLATFORM */

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
    safe_memset(&bt_ctx.discovered_devices, sizeof(bt_ctx.discovered_devices), 0, sizeof(bt_ctx.discovered_devices));
    safe_memset(&bt_ctx.paired_devices, sizeof(bt_ctx.paired_devices), 0, sizeof(bt_ctx.paired_devices));
    
#ifdef ESP_PLATFORM
    // NVS is initialized by main.c before calling bt_manager_init.
    // bt_manager assumes NVS is ready and uses nvs_storage_* functions.

    // Initialize Bluetooth controller
    esp_err_t ret;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.mode = ESP_BT_MODE_CLASSIC_BT;
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialize controller failed: %s (%d)", esp_err_to_name(ret), (int)ret);  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Controller initialized via bt_manager: mode=%d target_mode=0x%x", bt_cfg.mode, ESP_BT_MODE_CLASSIC_BT);  // NOLINT(bugprone-branch-clone)

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Enable controller failed: %s (%d)", esp_err_to_name(ret), (int)ret);  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Controller enabled via bt_manager: mode=CLASSIC_BT");  // NOLINT(bugprone-branch-clone)

    // Initialize Bluedroid
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialize bluedroid failed: %s", esp_err_to_name(ret));  // NOLINT(bugprone-branch-clone)
    return ESP_FAIL;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Enable bluedroid failed: %s", esp_err_to_name(ret));  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }

    // Configure device name
    // Use GAP API (not deprecated) to set the device name. esp_bt_dev_set_device_name is deprecated.
    esp_err_t _err_name = esp_bt_gap_set_device_name(config->device_name);
    if (_err_name != ESP_OK) {
        ESP_LOGW(TAG, "esp_bt_gap_set_device_name failed (%s)", esp_err_to_name(_err_name));  // NOLINT(bugprone-branch-clone)
    }

    // Register GAP callback
    esp_bt_gap_register_callback(bt_events_gap_callback);

    ret = bt_manager_init_profiles();
    if (ret != ESP_OK) {
        return ret;
    }

    // Set device discoverable and connectable
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    
    ESP_LOGI(TAG, "Bluetooth manager initialized with name: %s", config->device_name);  // NOLINT(bugprone-branch-clone)
    
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
        ESP_LOGI(TAG, "Loaded %d persisted paired devices", bt_ctx.paired_devices.count);  // NOLINT(bugprone-branch-clone)
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
    
    ESP_LOGI(TAG, "Bluetooth manager deinitialized");  // NOLINT(bugprone-branch-clone)
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
// CODE_REVIEW8 P2 Phase 3: Use thread-safe access via queue
int bt_manager_is_connected(void) {
#ifdef ESP_PLATFORM
    bt_manager_status_t status;
    if (bt_manager_get_status(&status) == ESP_OK) {
        return status.connected ? 1 : 0;
    }
    return 0; /* Timeout/error - assume not connected */
#else
    return bt_ctx.connected ? 1 : 0; /* Host tests: direct access OK (single-threaded) */
#endif
}

// Get the list of discovered devices
// UNSAFE: returns a direct pointer into bt_ctx which BtAppTask can modify.
// Use bt_get_device_list_snapshot() for concurrent-safe access.
bt_device_list_t* bt_get_device_list(void) {
    if (!bt_ctx.initialized) {
        return NULL;
    }

    return &bt_ctx.discovered_devices;
}

// Get the list of paired devices
// UNSAFE: returns a direct pointer into bt_ctx which BtAppTask can modify.
// Use bt_get_paired_devices_snapshot() for concurrent-safe access.
bt_device_list_t* bt_get_paired_devices(void) {
    if (!bt_ctx.initialized) {
        return NULL;
    }

    return &bt_ctx.paired_devices;
}

// Thread-safe snapshot: copies discovered device list into caller-supplied buffer.
// Safer than bt_get_device_list() because the caller holds a copy and is not
// affected by concurrent BtAppTask updates.  On the device, call only from a
// context that does not hold any BtAppTask-owned lock.
esp_err_t bt_get_device_list_snapshot(bt_device_list_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    safe_memcpy(out, sizeof(*out), &bt_ctx.discovered_devices, sizeof(bt_device_list_t));
    return ESP_OK;
}

// Thread-safe snapshot: copies paired device list into caller-supplied buffer.
esp_err_t bt_get_paired_devices_snapshot(bt_device_list_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    safe_memcpy(out, sizeof(*out), &bt_ctx.paired_devices, sizeof(bt_device_list_t));
    return ESP_OK;
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
        ESP_LOGE(TAG, "Failed to suspend audio stream");  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Suspended audio streaming");  // NOLINT(bugprone-branch-clone)
#else
    // For testing without ESP-IDF
    bt_ctx.audio_playing = false;
#endif
    
    return ESP_OK;
}

void bt_manager_set_autostart_enabled(bool enable) {
    s_autostart_enabled = enable;
    ESP_LOGI(TAG, "bt_manager: autostart %s", enable ? "ENABLED" : "DISABLED");  // NOLINT(bugprone-branch-clone)
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
            ESP_LOGW(TAG, "bt_start_audio: skipping start due to low memory: free_dram=%zu largest=%zu internal_free=%zu; not starting A2DP", free_dram, largest, internal_free);  // NOLINT(bugprone-branch-clone)
            return ESP_ERR_NO_MEM;
        }
    }

    // Start audio stream
    if (esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start audio stream");  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Started audio streaming");  // NOLINT(bugprone-branch-clone)
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
    ESP_LOGI(TAG, "Volume set to %d%%", volume);  // NOLINT(bugprone-branch-clone)
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
    if (!bt_pairing_parse_mac(mac, bda)) {
        ESP_LOGE(TAG, "Invalid MAC address format: %s", mac);  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_BT_MOCK_TESTING
    /* Under the mock-testing configuration use the higher-level pairing helper
     * so tests exercise the mock's deterministic flow instead of attempting to
     * touch the real controller. Capture the pending pairing state so command
     * handlers can accept PIN submissions later. */
    esp_err_t mock_err = bt_start_pairing(mac);
    if (mock_err == ESP_OK) {
        bt_pairing_method_t method = bt_mock_get_pairing_method();
        if (method == BT_PAIRING_METHOD_SSP) {
            char passkey_str[16] = {0};
            uint32_t passkey = 0;
            if (bt_mock_get_ssp_passkey(passkey_str, sizeof(passkey_str)) == ESP_OK) {
                passkey = (uint32_t)strtoul(passkey_str, NULL, 10);
            }
            bt_pairing_set_mock_state(bda, false, passkey);
        } else if (method == BT_PAIRING_METHOD_PIN) {
            bt_pairing_set_mock_state(bda, true, 0);
        } else {
            bt_pairing_prepare_for_initiation(bda);
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
                    ESP_LOGI(TAG, "Device %s already bonded; skipping pairing", mac);  // NOLINT(bugprone-branch-clone)
                    return ESP_OK;
                }
            }
        }
    }

    bt_pairing_prepare_for_initiation(bda);

    // Initiate GAP-level bonding by requesting the remote service list. The
    // controller will establish an ACL link and drive the authentication flow,
    // delivering PIN/SSP callbacks via bt_app_gap_callback.
    esp_err_t gap_err = esp_bt_gap_get_remote_services(bda);
    if (gap_err == ESP_OK) {
        ESP_LOGI(TAG, "Initiated GAP service discovery to pair with %s", mac);  // NOLINT(bugprone-branch-clone)
        return ESP_OK;
    }

    // Fall back to reading the remote name which also triggers an ACL link and
    // will still surface the required authentication callbacks.
    gap_err = esp_bt_gap_read_remote_name(bda);
    if (gap_err == ESP_OK) {
        ESP_LOGI(TAG, "Requested remote name to pair with %s", mac);  // NOLINT(bugprone-branch-clone)
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to initiate GAP bonding for %s: %s", mac, esp_err_to_name(gap_err));  // NOLINT(bugprone-branch-clone)
    bt_pairing_clear_pending();
    return gap_err;
#else
    esp_bd_addr_t bda = {0};
    if (!bt_pairing_parse_mac(mac, bda)) {
        return ESP_ERR_INVALID_ARG;
    }
    bt_pairing_prepare_for_initiation(bda);
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

    esp_bd_addr_t bda = {0};
    if (!bt_pairing_parse_mac(mac, bda)) {
        ESP_LOGE(TAG, "Invalid MAC address format: %s", mac);  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = esp_bt_gap_remove_bond_device(bda);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove controller bond for %s: %s", mac, esp_err_to_name(result));  // NOLINT(bugprone-branch-clone)
        goto exit;
    }

    esp_err_t storage_err = nvs_storage_remove_paired_device(mac);
    if (storage_err == ESP_OK) {
        ESP_LOGI(TAG, "Unpaired device: %s", mac);  // NOLINT(bugprone-branch-clone)
        result = ESP_OK;
    } else {
        const char* err_name = esp_err_to_name(storage_err);
        if (storage_err == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Controller bond for %s removed, but NVS entry missing (%s)", mac, err_name);  // NOLINT(bugprone-branch-clone)
        } else {
            ESP_LOGE(TAG, "Failed to purge NVS entry for %s: %s", mac, err_name);  // NOLINT(bugprone-branch-clone)
        }
        result = storage_err;
        goto exit;
    }

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
    
    int bonded_devices = esp_bt_gap_get_bond_device_num();
    if (bonded_devices < 0) {
        ESP_LOGW(TAG, "Unable to query bonded device count");  // NOLINT(bugprone-branch-clone)
        controller_status = ESP_FAIL;
    } else if (bonded_devices > 0) {
        int list_capacity = bonded_devices;
        esp_bd_addr_t *bond_list = (esp_bd_addr_t *)calloc((size_t)list_capacity, sizeof(esp_bd_addr_t));
        if (!bond_list) {
            ESP_LOGE(TAG, "Insufficient memory to retrieve bonded device list");  // NOLINT(bugprone-branch-clone)
            return ESP_ERR_NO_MEM;
        }
#ifdef UNIT_TEST
        bt_manager_test_record_unpair_all_temp_alloc(+1);
#endif

        esp_err_t list_err = esp_bt_gap_get_bond_device_list(&list_capacity, bond_list);
        if (list_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to fetch bonded devices: %s", esp_err_to_name(list_err));  // NOLINT(bugprone-branch-clone)
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
                    util_format_mac(bond_list[i], mac, sizeof(mac));
                    ESP_LOGE(TAG, "Failed to remove bond for %s: %s", mac, esp_err_to_name(err));  // NOLINT(bugprone-branch-clone)
                }
            }
        }

        free(bond_list);
    #ifdef UNIT_TEST
        bt_manager_test_record_unpair_all_temp_alloc(-1);
    #endif
    }

    if (controller_status == ESP_OK) {
        ESP_LOGI(TAG, "Removed %d bonded device(s) from controller", removed_count);  // NOLINT(bugprone-branch-clone)
    }

    int cleared_before = 0;
    (void)nvs_storage_get_paired_count(&cleared_before);

    esp_err_t storage_err = nvs_storage_clear_paired_devices();
    if (storage_err != ESP_OK && storage_err != ESP_ERR_NOT_FOUND) {
        if (controller_status == ESP_OK) {
            controller_status = storage_err;
        }
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Failed to clear paired devices from NVS: %s", esp_err_to_name(storage_err));  // NOLINT(bugprone-branch-clone)
#endif
    }

    safe_memset(&bt_ctx.paired_devices, sizeof(bt_ctx.paired_devices), 0, sizeof(bt_ctx.paired_devices));

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
    ESP_LOGI(TAG, "PIN code set to: %s, length: %d", pin, pin_len);  // NOLINT(bugprone-branch-clone)
#endif
    
    return ESP_OK;
}

// C-compatible wrappers expected by other components (command_interface)
// These provide a simple int-based return (0=success) while delegating to
// the bt_manager-style APIs above.
// Manager-prefixed C wrappers that forward to the bt_manager API. These are
// used by other components after we update their forward declarations.
int bt_manager_set_name(const char* name) {
#ifdef ESP_PLATFORM
    if (!bt_ctx.initialized) {
    	return -1;
    }
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
    /* Note: bt_start_scan() in bt_scan.c handles the test hook call */
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
        ESP_LOGE(TAG, "bt_manager_start_pair: failed to start pairing for %s: %s (%d)",  // NOLINT(bugprone-branch-clone)
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


#ifdef ESP_PLATFORM
static esp_err_t bt_manager_init_profiles(void)
{
    esp_err_t ret = esp_avrc_ct_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialize AVRCP controller failed: %s", esp_err_to_name(ret));  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }

    ret = esp_avrc_ct_register_callback(bt_events_avrc_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register AVRCP controller callback failed: %s", esp_err_to_name(ret));  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }

    ret = esp_a2d_source_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialize a2dp source failed: %s", esp_err_to_name(ret));  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }

    ret = esp_a2d_register_callback(bt_events_a2dp_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register a2dp source callback failed: %s", esp_err_to_name(ret));  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }

    ret = esp_a2d_source_register_data_callback(bt_events_a2dp_data_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register a2dp data callback failed: %s", esp_err_to_name(ret));  // NOLINT(bugprone-branch-clone)
        return ESP_FAIL;
    }

    return ESP_OK;
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
MAYBE_WEAK int bt_manager_forced_pair_failure(void) { return 0; }

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
MAYBE_WEAK void bt_source_mock_handle_audio_state(esp_a2d_audio_state_t state);
#endif
void bt_manager_test_invoke_a2dp_event(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
    if (!param) {
        return;
    }

    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            bt_events_handle_a2dp_connection(param);
            break;
        case ESP_A2D_AUDIO_STATE_EVT:
            bt_events_handle_a2dp_audio(param);
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
