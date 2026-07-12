/* bt_a2dp_test_shared.h — shared decls for the split bt_a2dp_test bodies
 * (runner + scenario group files), all linked into the test_bluetooth app. */
#ifndef BT_A2DP_TEST_SHARED_H
#define BT_A2DP_TEST_SHARED_H

#include "test_config.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "include/unity_config.h"
#include "unity.h"
#include "esp_log.h"
#include "esp_a2dp_api.h"
// Add required FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bt_mock_devices.h"
#include "bt_mock.h"
#include "bt_mock_setup.h"  // Update this include
#include "bt_test_setup.h"
#include "test_helpers.h"

// Test hook to inject A2DP events into the manager from device tests
void bt_manager_test_invoke_a2dp_event(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);

/* Test-only stub helper that clears the deferred disconnect visibility flag. */
void bt_source_stub_release_disconnect_visibility(void);

/* Forward declarations for pairing tests added in test_pairing_commands.c
 * These functions live in a separate translation unit; provide prototypes
 * so RUN_TEST can reference them without causing implicit/undeclared
 * identifier compile errors. */
void test_pairing_commands_happy_path(void);
void test_enter_pin_uses_default_when_missing(void);
void test_confirm_pin_without_pending_request_returns_error_event(void);

#define TAG "BT_A2DP_TEST"

/* Helpers defined (non-static) in bt_a2dp_test.c */
bool wait_for_connected_state(bool expected, int timeout_ms);
bool wait_for_authoritative_connected_state(bool expected, int timeout_ms);
void parse_test_addr(esp_bd_addr_t out);

/* Test bodies live in bt_a2dp_test_{scan,connection,streaming}.c */
void test_bluetooth_stack_init(void);
void test_bluetooth_scan_start(void);
void test_bluetooth_scan_discovered_devices(void);
void test_bluetooth_scan_filter_by_type(void);
void test_bluetooth_scanning_basic(void);
void test_bluetooth_scan_device_details(void);
void test_bluetooth_scan_timeout(void);
void test_bluetooth_scan_stop_early(void);
void test_bluetooth_connection(void);
void test_connect_by_name(void);
void test_connection_failure_handling(void);
void test_connection_timeout(void);
void test_connection_status_info(void);
void test_auto_reconnect(void);
void test_auto_reconnect_should_stop_after_failed_attempts(void);
void test_auto_reconnect_should_apply_configured_delay(void);
void test_auto_reconnect_should_stop_after_first_success(void);
void test_auto_reconnect_disabled_should_skip_attempts(void);
void test_auto_reconnect_failures_clear_streaming_state(void);
void test_connect_to_a2dp_sink(void);
void test_a2dp_streaming(void);
void test_a2dp_paired_devices(void);
void test_audio_streaming_start_success(void);
void test_audio_streaming_stop_success(void);
void test_streaming_requires_connection(void);
void test_streaming_pause_resume(void);
void test_streaming_state_reporting(void);
void test_remote_suspend_and_resume_should_toggle_stream_state(void);
void test_disconnect_during_streaming_should_reconnect_and_stop_stream(void);

#endif /* BT_A2DP_TEST_SHARED_H */
