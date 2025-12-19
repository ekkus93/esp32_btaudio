/* Auto-generated runner: calls every compiled test function in this app
 * This file was added to ensure all test_... functions present in the
 * test binary are actually exercised by the on-device Unity harness.
 */
#include "unity.h"
#include "esp_log.h"

static const char *TAG = "AUTO_RUN_TESTS";

/* Declarations for test functions that are compiled into the ELF.
 * These are discovered from the build output and declared `extern` here
 * so we can invoke them via RUN_TEST. Keep this list in sync with the
 * compiled symbols if sources change.
 */
extern void test_bluetooth_stack_init(void);
extern void test_bluetooth_scan_start(void);
extern void test_bluetooth_scan_discovered_devices(void);
extern void test_bluetooth_scan_filter_by_type(void);
extern void test_bluetooth_scanning_basic(void);
extern void test_bluetooth_scan_device_details(void);
extern void test_bluetooth_scan_timeout(void);
extern void test_bluetooth_scan_stop_early(void);
extern void test_bluetooth_connection(void);
extern void test_connect_by_name(void);
extern void test_bt_auto_reconnect(void);
extern void test_bt_connect_by_address(void);
extern void test_bt_connect_by_name(void);
extern void test_bt_connect_failure(void);
extern void test_bt_connection_info(void);
extern void test_bt_connect_timeout(void);
extern void test_connection_failure_handling(void);
extern void test_connection_timeout(void);
extern void test_connection_status_info(void);
extern void test_auto_reconnect(void);
extern void test_connect_to_a2dp_sink(void);
extern void test_a2dp_streaming(void);
extern void test_a2dp_paired_devices(void);
extern void test_audio_streaming_start_success(void);
extern void test_audio_streaming_stop_success(void);
extern void test_streaming_requires_connection(void);
extern void test_streaming_pause_resume(void);
extern void test_streaming_state_reporting(void);
extern void test_pairing_commands_happy_path(void);
extern void test_enter_pin_uses_default_when_missing(void);
extern void test_confirm_pin_without_pending_request_returns_error_event(void);
extern void test_pin_pairing_success(void);
extern void test_pin_pairing_failure(void);
extern void test_pin_pairing_initiation(void);
extern void test_pin_pairing_timeout(void);
extern void test_set_default_pin(void);
extern void test_unpair_specific_device(void);
extern void test_unpair_all_devices(void);
extern void test_ssp_confirmation_request(void);
extern void test_ssp_confirmation_accepted(void);
extern void test_ssp_confirmation_rejected(void);
extern void test_ssp_fallback_to_pin(void);
extern void test_confirm_pin_without_pending_request_returns_error_event(void);
extern void test_push_event(void);
extern void test_utils_reset_state(void);
extern void test_cmd_parse_and_execute_debug_mock_add(void);

/* Wrapper that runs all tests via RUN_TEST. This is called from the
 * `app_test_main` harness where UNITY_BEGIN() has already been called.
 */
void run_auto_tests(void)
{
    ESP_LOGI(TAG, "Running auto-generated test list (%s)", __FILE__);

    /* Call tests. Use RUN_TEST so Unity records them correctly. */
    RUN_TEST(test_bluetooth_stack_init, __LINE__);
    RUN_TEST(test_bluetooth_scan_start, __LINE__);
    RUN_TEST(test_bluetooth_scan_discovered_devices, __LINE__);
    RUN_TEST(test_bluetooth_scan_filter_by_type, __LINE__);
    RUN_TEST(test_bluetooth_scanning_basic, __LINE__);
    RUN_TEST(test_bluetooth_scan_device_details, __LINE__);
    RUN_TEST(test_bluetooth_scan_timeout, __LINE__);
    RUN_TEST(test_bluetooth_scan_stop_early, __LINE__);
    RUN_TEST(test_bluetooth_connection, __LINE__);
    RUN_TEST(test_connect_by_name, __LINE__);
    /* additional bt_connect_* tests (declared in other sources) */
    RUN_TEST(test_bt_auto_reconnect, __LINE__);
    RUN_TEST(test_bt_connect_by_address, __LINE__);
    RUN_TEST(test_bt_connect_by_name, __LINE__);
    RUN_TEST(test_bt_connect_failure, __LINE__);
    RUN_TEST(test_bt_connection_info, __LINE__);
    RUN_TEST(test_bt_connect_timeout, __LINE__);
    RUN_TEST(test_connection_failure_handling, __LINE__);
    RUN_TEST(test_connection_timeout, __LINE__);
    RUN_TEST(test_connection_status_info, __LINE__);
    RUN_TEST(test_auto_reconnect, __LINE__);
    RUN_TEST(test_connect_to_a2dp_sink, __LINE__);
    RUN_TEST(test_a2dp_streaming, __LINE__);
    RUN_TEST(test_a2dp_paired_devices, __LINE__);
    RUN_TEST(test_audio_streaming_start_success, __LINE__);
    RUN_TEST(test_audio_streaming_stop_success, __LINE__);
    RUN_TEST(test_streaming_requires_connection, __LINE__);
    RUN_TEST(test_streaming_pause_resume, __LINE__);
    RUN_TEST(test_streaming_state_reporting, __LINE__);
    RUN_TEST(test_pairing_commands_happy_path, __LINE__);
    RUN_TEST(test_enter_pin_uses_default_when_missing, __LINE__);
    RUN_TEST(test_confirm_pin_without_pending_request_returns_error_event, __LINE__);
    RUN_TEST(test_pin_pairing_success, __LINE__);
    RUN_TEST(test_pin_pairing_failure, __LINE__);
    RUN_TEST(test_pin_pairing_initiation, __LINE__);
    RUN_TEST(test_pin_pairing_timeout, __LINE__);
    RUN_TEST(test_set_default_pin, __LINE__);
    RUN_TEST(test_unpair_specific_device, __LINE__);
    RUN_TEST(test_unpair_all_devices, __LINE__);
    RUN_TEST(test_ssp_confirmation_request, __LINE__);
    RUN_TEST(test_ssp_confirmation_accepted, __LINE__);
    RUN_TEST(test_ssp_confirmation_rejected, __LINE__);
    RUN_TEST(test_ssp_fallback_to_pin, __LINE__);
    /* duplicate name intentionally declared earlier in some files; calling once is sufficient */
    /* RUN_TEST(test_confirm_pin_without_pending_request_returns_error_event, __LINE__); */
    RUN_TEST(test_push_event, __LINE__);
    RUN_TEST(test_utils_reset_state, __LINE__);
    /* Command parser tests */
    RUN_TEST(test_cmd_parse_and_execute_debug_mock_add, __LINE__);

}
