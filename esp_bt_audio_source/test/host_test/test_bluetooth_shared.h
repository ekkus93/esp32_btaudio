/* test_bluetooth_shared.h — shared decls for the split test_bluetooth executable
 * (runner test_bluetooth.c + bodies test_bluetooth_cases.c). Not a public header. */
#ifndef TEST_BLUETOOTH_SHARED_H
#define TEST_BLUETOOTH_SHARED_H

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "bt_manager.h"
#include "esp_err.h"

/* bt_manager_status_t is defined in bt_source.h, but including that header
 * creates a conflict with bt_device_t from bt_manager.h. Forward-declare the
 * struct here for the snapshot test. */
typedef struct {
    bool initialized;
    bool connected;
    bool audio_playing;
    bool scanning;
    char connected_mac[18];
    char connected_name[32];
} bt_manager_status_t;

/* bt_manager_get_status() is defined in bt_manager.c. It's not exposed via
 * bt_manager.h, so we declare it here for the snapshot test. */
esp_err_t bt_manager_get_status(bt_manager_status_t *status);
#include "mock_i2s.h"
#include "esp_bt.h"
#include "nvs_storage.h"

// Host nvs mock controls
void nvs_storage_mock_set_init_result(esp_err_t err);
void nvs_storage_mock_set_get_count_result(esp_err_t err);
void nvs_storage_mock_set_get_device_result(esp_err_t err);
void nvs_storage_mock_reset(void);

// Test-hook setters from mocks/bt_manager_test_hooks.c
void bt_manager_test_set_force_disconnect_failure(int v);
void bt_manager_test_set_force_start_failure(int v);
void bt_manager_test_set_force_stop_failure(int v);
void bt_manager_test_reset_forces(void);
int bt_manager_test_get_scan_start_count(void);
void bt_manager_test_reset_btstate_mock(void);
int bt_manager_test_get_start_audio_calls(void);
int bt_manager_test_get_last_conn_state(void);
int bt_manager_test_get_last_audio_state(void);
int bt_manager_test_get_autostart_attempts(void);
void bt_manager_test_reset_autostart_attempts(void);
int bt_manager_test_get_pair_event_count(void);
const char* bt_manager_test_get_last_pair_event_subtype(void);
const char* bt_manager_test_get_last_pair_event_data(void);
void bt_manager_test_invoke_a2dp_event(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);
// Manager wrapper prototypes (not exposed via header)
int bt_manager_disconnect(void);
int bt_manager_start_audio(void);
int bt_manager_stop_audio(void);

// Pairing pending helpers (UNIT_TEST only)
void bt_manager_test_reset_pending(void);
bool bt_manager_test_gap_pin_request(const char* mac);
bool bt_manager_test_gap_ssp_confirm(const char* mac, uint32_t passkey);
void bt_manager_test_gap_auth_complete(const char* mac, bool success);
bool bt_pairing_get_pending_request(bt_pairing_request_info_t* info);
bool bt_manager_test_is_audio_playing(void);

// Autostart helper (UNIT_TEST only)
bool bt_manager_test_autostart_on_connect(void);
void bt_manager_set_autostart_enabled(bool enable);
bool bt_manager_is_autostart_enabled(void);

#ifdef UNIT_TEST
void bt_manager_force_initialized(bool value);
void bt_manager_debug_print(void);
#endif

/* Fixture state + mock callbacks — DEFINED in test_bluetooth.c (runner). */
extern bool bt_connected_callback_called;
extern bool bt_disconnected_callback_called;
extern char bt_connected_mac[18];
extern char bt_connected_name[32];
extern char bt_disconnected_mac[18];
void test_bt_connected_cb(const char* mac, const char* name);
void test_bt_disconnected_cb(const char* mac);

/* Test bodies live in test_bluetooth_cases.c */
void test_bt_init_deinit(void);
void test_bt_scanning(void);
void test_bt_connect_disconnect(void);
void test_bt_connect_by_name(void);
void test_bt_audio_operations(void);
void test_bt_scan_hook_counts(void);
void test_bt_scan_requires_init(void);
void test_bt_pairing(void);
void test_bt_scan_ignores_when_not_scanning(void);
void test_bt_pairing_pending_out_of_order(void);
void test_bt_autostart_guard_when_playing(void);
void test_bt_a2dp_connection_respects_autostart_disable(void);
void test_bt_a2dp_connection_autostart_and_forwarding(void);
void test_bt_a2dp_audio_state_forwarding(void);
void test_bt_a2dp_remote_suspend_clears_playing(void);
void test_bt_a2dp_remote_suspend_then_resume(void);
void test_bt_gap_failure_paths_emit_events_and_clear_pending(void);
void test_bt_gap_auth_failure_allows_retry(void);
void test_bt_gap_success_emits_success_and_clears_pending(void);
void test_bt_gap_events_emit_command_events(void);
void test_bt_autostart_resets_between_sessions(void);
void test_bt_a2dp_disconnect_and_stop_clear_playing(void);
void test_bt_disconnect_failure_then_success(void);
void test_bt_start_stop_failure_recovery(void);
void test_bt_init_survives_nvs_failures(void);
void test_bt_init_skips_corrupt_paired_device_entries(void);
void test_bt_stop_failure_then_recovery_on_state_event(void);

#endif /* TEST_BLUETOOTH_SHARED_H */
