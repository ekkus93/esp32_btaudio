#include "bt_manager.h"
#include "bt_manager_internal.h"
#include "platform_memory.h"
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
/* Unit test tracking for auto-start attempts - accessed by event handlers */
int s_autostart_attempts = 0;
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

void bt_manager_set_autostart_enabled(bool enable) {
    s_autostart_enabled = enable;
    ESP_LOGI(TAG, "bt_manager: autostart %s", enable ? "ENABLED" : "DISABLED");  // NOLINT(bugprone-branch-clone)
}

bool bt_manager_is_autostart_enabled(void) {
    return s_autostart_enabled;
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

    /* DESIGN: bt_events_a2dp_callback is the SINGLE registered A2DP callback.
     * It dispatches to bt_connection_state_cb / bt_audio_state_cb in
     * bt_connection_manager.  bt_connection_manager_init() also calls
     * esp_a2d_register_callback() but that function is only used by isolated
     * unit tests that need a minimal connection-manager harness; it is never
     * called in production.  Do not add a second esp_a2d_register_callback()
     * registration here — only the last registration wins in ESP-IDF and a
     * second call would silently drop events for the other handler. */
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
