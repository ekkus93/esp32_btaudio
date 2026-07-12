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

/*
 * Weak test hooks used by host tests to inject failures. Production builds
 * fall back to these no-op implementations so linking succeeds when the test
 * overrides are absent.
 */
MAYBE_WEAK int bt_manager_forced_disconnect_failure(void) { return 0; }
MAYBE_WEAK int bt_manager_forced_start_failure(void) { return 0; }
MAYBE_WEAK int bt_manager_forced_stop_failure(void) { return 0; }

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
    bt_ctx.connecting = false;
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
    bt_ctx.connecting = false;
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
