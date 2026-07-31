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
#include "bt_hfp_ag.h"
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

/* A failed teardown may leave Bluetooth callbacks alive. In that case the
 * manager is quarantined until reboot and init is rejected. */
static bool s_bt_manager_quarantined;

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
// Audio processor API - used by A2DP data callback to pull PCM
#include "audio_processor.h"
#endif

#ifdef UNIT_TEST
/* Unit test tracking for auto-start attempts - accessed by event handlers */
int s_autostart_attempts = 0;
#endif

/* ============================================================================
 * BT Manager Status API — mutex-protected snapshot (RH-WR-01)
 *
 * bt_manager_get_status() now acquires s_bt_ctx_mutex, copies the status
 * fields into a local snapshot, then releases the mutex.  This replaces
 * the previous request/queue pattern (BtAppTask + semaphore) and is
 * simpler, faster, and safer.
 *
 * Callbacks are never invoked while holding the mutex (see spec callback
 * rule).
 * ============================================================================ */

#ifdef ESP_PLATFORM
/**
 * @brief Get BT manager status from ANY task (thread-safe via mutex)
 *
 * This is the PUBLIC API that command handlers and other components should call
 * to read BT manager state.  Acquires s_bt_ctx_mutex, copies the snapshot,
 * releases the mutex, and returns the result.
 *
 * @param[out] status Pointer to response structure
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if status is NULL,
 *         ESP_ERR_TIMEOUT if mutex lock timed out
 * @note Blocks up to 100ms waiting for mutex. Safe to call from any task.
 */
MAYBE_WEAK esp_err_t bt_manager_get_status(bt_manager_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = bt_ctx_lock(100);
    if (err != ESP_OK) {
        return err;
    }

    /* Copy snapshot under lock */
    status->initialized = bt_ctx.initialized;
    status->connected = bt_ctx.connected;
    status->audio_playing = bt_ctx.audio_playing;
    status->scanning = bt_ctx.scanning;
    safe_copy_str(status->connected_mac, sizeof(status->connected_mac),
                  bt_ctx.connected_mac);
    safe_copy_str(status->connected_name, sizeof(status->connected_name),
                  bt_ctx.connected_name);

    bt_ctx_unlock();
    return ESP_OK;
}
#endif

static esp_err_t bt_manager_finalize_teardown(esp_err_t first_error,
                                               bool callbacks_stopped)
{
    if (!callbacks_stopped) {
        /* Callback-owned identity must remain intact while a late Bluedroid
         * callback is still possible. Reinitialization is blocked by the
         * quarantine gate until reboot. */
        ESP_LOGE(TAG, "Bluedroid shutdown was not confirmed; preserving callback-owned state and quarantining manager");
        s_bt_manager_quarantined = true;
        return first_error != ESP_OK ? first_error : ESP_ERR_INVALID_STATE;
    }

    esp_err_t reset_err = bt_events_a2dp_reset_binding();
    if (reset_err != ESP_OK) {
        ESP_LOGE(TAG, "A2DP binding reset during deinit failed: %s",
                 esp_err_to_name(reset_err));
        if (first_error == ESP_OK) {
            first_error = reset_err;
        }
        /* The guarded binding could not be cleared safely. Preserve both the
         * manager state and its mutex, then quarantine until reboot. */
        s_bt_manager_quarantined = true;
        return first_error;
    }

    bt_ctx.initialized = false;
    bt_ctx.scanning = false;
    bt_ctx.connected = false;
    bt_ctx.connecting = false;
    bt_ctx.audio_playing = false;

    if (first_error != ESP_OK) {
        s_bt_manager_quarantined = true;
    }

    platform_mutex_delete(s_bt_ctx_mutex);
    s_bt_ctx_mutex = NULL;
    return first_error;
}

/* Complete the callback-owned portion of init rollback using the facts gathered
 * by the real ESP-platform reverse-order cleanup. The original initialization
 * error is always authoritative. A secondary reset failure is logged, retains
 * the mutex and binding, and quarantines the manager rather than fabricating a
 * clean rollback. */
static esp_err_t bt_manager_finalize_init_rollback(
    esp_err_t init_error,
    bool callbacks_stopped,
    bool cleanup_complete,
    bool duplex_state_initialized)
{
    if (!callbacks_stopped) {
        ESP_LOGE(TAG,
                 "Bluedroid shutdown was not confirmed after init error %s; preserving callback-owned state and quarantining manager",
                 esp_err_to_name(init_error));
        s_bt_manager_quarantined = true;
        return init_error;
    }

    bt_hfp_ag_force_cleanup_after_stack_shutdown();
    if (duplex_state_initialized) {
        bt_duplex_state_deinit();
    }

    esp_err_t binding_reset_err = bt_events_a2dp_reset_binding();
    if (binding_reset_err != ESP_OK) {
        cleanup_complete = false;
        ESP_LOGE(TAG,
                 "A2DP binding reset rollback failed after init error %s: %s",
                 esp_err_to_name(init_error),
                 esp_err_to_name(binding_reset_err));
        /* The guarded reset did not complete. Keep the mutex and binding intact
         * for any state that may still be inspected during quarantine. */
    } else {
        platform_mutex_delete(s_bt_ctx_mutex);
        s_bt_ctx_mutex = NULL;
    }

    if (!cleanup_complete) {
        s_bt_manager_quarantined = true;
    }
    return init_error;
}

// Initialize Bluetooth Manager
MAYBE_WEAK bt_err_t bt_manager_init(const bt_manager_init_t* config) {
    if (config == NULL || config->device_name == NULL) {
        return ESP_FAIL;
    }

    if (s_bt_manager_quarantined) {
        ESP_LOGE(TAG, "Bluetooth manager is quarantined after an incomplete teardown; reboot required");
        return ESP_ERR_INVALID_STATE;
    }

    if (bt_ctx.initialized) {
        return ESP_OK; // Already initialized
    }

    /* Create the bt_ctx mutex before resetting any state guarded by it. No
     * Bluetooth callbacks are registered until after this reset succeeds. */
    s_bt_ctx_mutex = platform_mutex_create();
    if (s_bt_ctx_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create bt_ctx mutex");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t reset_err = bt_events_a2dp_reset_binding();
    if (reset_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset A2DP binding during init: %s",
                 esp_err_to_name(reset_err));
        platform_mutex_delete(s_bt_ctx_mutex);
        s_bt_ctx_mutex = NULL;
        return reset_err;
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

    // Track which stages completed for rollback safety (RH-WR-05)
    bool controller_init_done = false;
    bool controller_enabled = false;
    bool bluedroid_init_done = false;
    bool bluedroid_enabled = false;
    bool duplex_state_initialized = false;
    bool profiles_initialized = false;

    esp_err_t ret = bt_duplex_state_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialize duplex state failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    duplex_state_initialized = true;

    // Initialize Bluetooth controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.mode = ESP_BT_MODE_CLASSIC_BT;
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialize controller failed: %s (%d)", esp_err_to_name(ret), (int)ret);  // NOLINT(bugprone-branch-clone)
        goto fail;
    }
    controller_init_done = true;
    ESP_LOGI(TAG, "Controller initialized via bt_manager: mode=%d target_mode=0x%x", bt_cfg.mode, ESP_BT_MODE_CLASSIC_BT);  // NOLINT(bugprone-branch-clone)

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Enable controller failed: %s (%d)", esp_err_to_name(ret), (int)ret);  // NOLINT(bugprone-branch-clone)
        goto fail;
    }
    controller_enabled = true;
    ESP_LOGI(TAG, "Controller enabled via bt_manager: mode=CLASSIC_BT");  // NOLINT(bugprone-branch-clone)

    // Initialize Bluedroid
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialize bluedroid failed: %s", esp_err_to_name(ret));  // NOLINT(bugprone-branch-clone)
        goto fail;
    }
    bluedroid_init_done = true;

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Enable bluedroid failed: %s", esp_err_to_name(ret));  // NOLINT(bugprone-branch-clone)
        goto fail;
    }
    bluedroid_enabled = true;

    ret = esp_bt_gap_set_device_name(config->device_name);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set Bluetooth device name failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    ret = esp_bt_gap_register_callback(bt_events_gap_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register GAP callback failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    ret = bt_manager_init_profiles();
    if (ret != ESP_OK) {
        goto fail;
    }
    profiles_initialized = true;

    ret = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
                                   ESP_BT_GENERAL_DISCOVERABLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set GAP scan mode failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    
    ESP_LOGI(TAG, "Bluetooth manager initialized with name: %s", config->device_name);  // NOLINT(bugprone-branch-clone)
    
    int count = 0;
    if (nvs_storage_get_paired_count(&count) == ESP_OK && count > 0) {
        for (int i = 0; i < count && bt_ctx.paired_devices.count < 20; i++) {
            char mac[32] = {0};
            char name[32] = {0};
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

    bt_ctx.initialized = true;
    return ESP_OK;

fail:
    /* Roll back in reverse order. Preserve the original initialization error.
     * Do not free callback-owned state unless Bluedroid deinit confirms that
     * callbacks can no longer arrive. */
    bool cleanup_complete = true;
    bool callbacks_stopped = !bluedroid_init_done;
    if (profiles_initialized) {
        esp_err_t cleanup_err = bt_manager_deinit_profiles();
        if (cleanup_err != ESP_OK) {
            cleanup_complete = false;
            ESP_LOGE(TAG, "Profile rollback failed after init error %s: %s",
                     esp_err_to_name(ret), esp_err_to_name(cleanup_err));
        }
    }
    if (bluedroid_enabled) {
        esp_err_t cleanup_err = esp_bluedroid_disable();
        if (cleanup_err != ESP_OK) {
            cleanup_complete = false;
            ESP_LOGE(TAG, "Bluedroid disable rollback failed: %s",
                     esp_err_to_name(cleanup_err));
        }
    }
    if (bluedroid_init_done) {
        esp_err_t cleanup_err = esp_bluedroid_deinit();
        if (cleanup_err != ESP_OK) {
            cleanup_complete = false;
            ESP_LOGE(TAG, "Bluedroid deinit rollback failed: %s",
                     esp_err_to_name(cleanup_err));
        } else {
            callbacks_stopped = true;
        }
    }
    if (controller_enabled) {
        esp_err_t cleanup_err = esp_bt_controller_disable();
        if (cleanup_err != ESP_OK) {
            cleanup_complete = false;
            ESP_LOGE(TAG, "Controller disable rollback failed: %s",
                     esp_err_to_name(cleanup_err));
        }
    }
    if (controller_init_done) {
        esp_err_t cleanup_err = esp_bt_controller_deinit();
        if (cleanup_err != ESP_OK) {
            cleanup_complete = false;
            ESP_LOGE(TAG, "Controller deinit rollback failed: %s",
                     esp_err_to_name(cleanup_err));
        }
    }

    return bt_manager_finalize_init_rollback(
        ret, callbacks_stopped, cleanup_complete, duplex_state_initialized);

#else
    bt_ctx.initialized = true;
    return ESP_OK;
#endif
}

// Deinitialize Bluetooth Manager
 bt_err_t bt_manager_deinit(void) {
    if (!bt_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t first_error = ESP_OK;
    bool callbacks_stopped = true;
#ifdef ESP_PLATFORM
    callbacks_stopped = false;
#define RECORD_DEINIT_ERROR(expr, label) do {                               \
        esp_err_t _err = (expr);                                            \
        if (_err != ESP_OK) {                                               \
            ESP_LOGE(TAG, label " failed: %s", esp_err_to_name(_err));      \
            if (first_error == ESP_OK) first_error = _err;                   \
        }                                                                   \
    } while (0)

    if (bt_ctx.scanning) {
        RECORD_DEINIT_ERROR(bt_stop_scan(), "Stop scan during deinit");
    }
    if (bt_ctx.connected || bt_ctx.connecting) {
        RECORD_DEINIT_ERROR(bt_disconnect(), "Disconnect during deinit");
    }

    RECORD_DEINIT_ERROR(bt_manager_deinit_profiles(), "Profile deinit");
    RECORD_DEINIT_ERROR(esp_bluedroid_disable(), "Bluedroid disable");
    esp_err_t bluedroid_deinit_err = esp_bluedroid_deinit();
    if (bluedroid_deinit_err != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid deinit failed: %s",
                 esp_err_to_name(bluedroid_deinit_err));
        if (first_error == ESP_OK) first_error = bluedroid_deinit_err;
    } else {
        callbacks_stopped = true;
    }
    RECORD_DEINIT_ERROR(esp_bt_controller_disable(), "Controller disable");
    RECORD_DEINIT_ERROR(esp_bt_controller_deinit(), "Controller deinit");

    if (callbacks_stopped) {
        bt_hfp_ag_force_cleanup_after_stack_shutdown();
        bt_duplex_state_deinit();
        ESP_LOGI(TAG, "Bluetooth manager deinitialized");
    }
#undef RECORD_DEINIT_ERROR
#endif

    return bt_manager_finalize_teardown(first_error, callbacks_stopped);
}

MAYBE_WEAK int bt_manager_is_connected(void) {
#ifdef ESP_PLATFORM
    bt_manager_status_t status;
    if (bt_manager_get_status(&status) == ESP_OK) {
        return status.connected ? 1 : 0;
    }
    return 0;
#else
    return bt_ctx.connected ? 1 : 0;
#endif
}

bt_device_list_t* bt_get_device_list(void) {
    if (!bt_ctx.initialized) return NULL;
    return &bt_ctx.discovered_devices;
}

bt_device_list_t* bt_get_paired_devices(void) {
    if (!bt_ctx.initialized) return NULL;
    return &bt_ctx.paired_devices;
}

esp_err_t bt_get_device_list_snapshot(bt_device_list_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    safe_memcpy(out, sizeof(*out), &bt_ctx.discovered_devices, sizeof(bt_device_list_t));
    bt_ctx_unlock();
    return ESP_OK;
}

esp_err_t bt_get_paired_devices_snapshot(bt_device_list_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    safe_memcpy(out, sizeof(*out), &bt_ctx.paired_devices, sizeof(bt_device_list_t));
    bt_ctx_unlock();
    return ESP_OK;
}

void bt_manager_set_autostart_enabled(bool enable) {
    s_autostart_enabled = enable;
    ESP_LOGI(TAG, "bt_manager: autostart %s", enable ? "ENABLED" : "DISABLED");
}

bool bt_manager_is_autostart_enabled(void) {
    return s_autostart_enabled;
}

MAYBE_WEAK int bt_manager_set_name(const char* name) {
#ifdef ESP_PLATFORM
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return -1;
    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return -1;
    }
    bt_ctx_unlock();
    err = esp_bt_gap_set_device_name(name);
    return (err == ESP_OK) ? 0 : -1;
#else
    (void)name;
    return 0;
#endif
}

int bt_manager_pair(const char* mac) {
    return (bt_pair(mac) == ESP_OK) ? 0 : -1;
}

MAYBE_WEAK int bt_manager_connect(const char* mac) {
    return (bt_connect(mac) == ESP_OK) ? 0 : -1;
}

MAYBE_WEAK int bt_manager_disconnect(void) {
#ifdef UNIT_TEST
    extern int bt_manager_forced_disconnect_failure(void);
    if (bt_manager_forced_disconnect_failure()) return -1;
#endif
    return (bt_disconnect() == ESP_OK) ? 0 : -1;
}

MAYBE_WEAK int bt_manager_start_audio(void) {
#ifdef UNIT_TEST
    extern int bt_manager_forced_start_failure(void);
    if (bt_manager_forced_start_failure()) return -1;
#endif
    return (bt_start_audio() == ESP_OK) ? 0 : -1;
}

MAYBE_WEAK int bt_manager_start_scan(void) {
#ifdef UNIT_TEST
    extern int bt_manager_forced_start_failure(void);
    if (bt_manager_forced_start_failure()) return -1;
#endif
    return (bt_start_scan() == ESP_OK) ? 0 : -1;
}

MAYBE_WEAK int bt_manager_start_pair(const char* mac) {
#ifdef UNIT_TEST
    extern int bt_manager_forced_pair_failure(void);
    if (bt_manager_forced_pair_failure()) return -1;
#endif
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
        bt_manager_test_record_pair_start(mac);
    }
#endif
    return ret;
}

MAYBE_WEAK int bt_manager_stop_audio(void) {
#ifdef UNIT_TEST
    extern int bt_manager_forced_stop_failure(void);
    if (bt_manager_forced_stop_failure()) return -1;
#endif
    return (bt_stop_audio() == ESP_OK) ? 0 : -1;
}

#ifdef UNIT_TEST
bool bt_manager_test_is_quarantined(void)
{
    return s_bt_manager_quarantined;
}

esp_err_t bt_manager_test_finalize_teardown(esp_err_t first_error,
                                             bool callbacks_stopped)
{
    return bt_manager_finalize_teardown(first_error, callbacks_stopped);
}

esp_err_t bt_manager_test_finalize_init_rollback(
    esp_err_t init_error,
    bool callbacks_stopped,
    bool cleanup_complete,
    bool duplex_state_initialized)
{
    return bt_manager_finalize_init_rollback(
        init_error,
        callbacks_stopped,
        cleanup_complete,
        duplex_state_initialized);
}
#endif
