#include "test_commands_shared.h"

/* Test-only hook defined in bt_manager.c under UNIT_TEST */
void bt_manager_test_set_connection_state(int v);


const char* cmd_version_host_override(void)
{
    return "TEST-HOST-VERSION";
}
// Access mock gap helpers
extern void mock_gap_reset(void);
extern const char* mock_gap_get_last_mac(void);
extern int mock_gap_get_last_pin_len(void);
extern const char* mock_gap_get_last_pin(void);
extern int mock_gap_get_last_confirm(void);
extern void bt_manager_test_reset_forces(void);
extern int bt_manager_test_get_scan_start_count(void);
extern const char* bt_manager_test_get_last_unpair_mac(void);
extern void bt_manager_test_set_force_unpair_failure(int v);
extern void bt_manager_test_set_force_unpair_all_failure(int v);
extern int bt_manager_test_get_unpair_all_removed(void);
extern int bt_manager_test_get_unpair_all_cleared_before(void);

// Test fixture
void setUp(void) {
    // Initialize before each test
    mock_uart_init(115200);
    cmd_init();
    cmd_test_reset_cmd_process_state();
    bt_manager_init_t cfg = {
        .device_name = "MockBT",
        .connected_cb = NULL,
        .disconnected_cb = NULL,
    };
    bt_manager_init(&cfg);
    bt_manager_test_reset_forces();
    bt_manager_test_set_force_unpair_all_failure(0);
    nvs_storage_clear_paired_devices();
}

void tearDown(void) {
    // Clean up after each test
    cmd_deinit();
}


// Test basic command parsing

// Test command with parameter

// Test multiple parameters


// Test invalid command


// Test command with whitespace


/* BUG-3: truly empty string must not UB on s-1 pointer arithmetic */


// Test response generation

// Test command processing from UART


int count_substring(const char* haystack, const char* needle)
{
    int count = 0;
    const char* pos = haystack;
    size_t step = strlen(needle);
    while (pos && (pos = strstr(pos, needle)) != NULL) {
        ++count;
        pos += step;
    }
    return count;
}


// Verify issuing SCAN triggers the manager start-scan path (unit-test builds)

// New tests for pairing command handlers


// Main test runner
/* TEST-8: cmd_status_to_name() — all defined codes return non-empty strings */

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_parse_scan_command);
    RUN_TEST(test_parse_connect_command);
    RUN_TEST(test_parse_i2s_config_command);
    RUN_TEST(test_parse_i2s_config_command_with_format);
    RUN_TEST(test_parse_invalid_command);
    RUN_TEST(test_parse_malformed_tokens);
    RUN_TEST(test_parse_command_with_whitespace);
    RUN_TEST(test_parse_empty_command_should_error);
    RUN_TEST(test_parse_truly_empty_string_should_error);
    RUN_TEST(test_parse_whitespace_only_should_error);
    RUN_TEST(test_parse_limits_param_count_and_truncates);
    RUN_TEST(test_parse_connect_name_preserves_spaces);
    RUN_TEST(test_parse_diag_command);
    RUN_TEST(test_send_response);
    RUN_TEST(test_command_processing);
    RUN_TEST(test_cmd_process_handles_multiple_commands_in_one_read);
    RUN_TEST(test_cmd_process_accumulates_partial_line_across_calls);
    RUN_TEST(test_cmd_process_recovers_after_overflow_reset);
    RUN_TEST(test_debug_log_sets_level_and_response);
    RUN_TEST(test_help_command);
    RUN_TEST(test_version_command);
    RUN_TEST(test_diag_command_reports_state);
    RUN_TEST(test_scan_invokes_manager);

    // Pairing related tests
    RUN_TEST(test_confirm_pin_command);
    RUN_TEST(test_enter_pin_command);
    RUN_TEST(test_volume_invalid_param_should_error);
    RUN_TEST(test_volume_missing_param_should_error);
    RUN_TEST(test_i2s_config_invalid_rate_should_error);
    RUN_TEST(test_i2s_config_invalid_bit_depth_should_error);
    RUN_TEST(test_i2s_config_invalid_channels_should_error);
    RUN_TEST(test_debug_mock_add_missing_param_errors);
    
    // Forward-declare tests implemented below
    extern void test_mute_unmute_command(void);
    extern void test_unpair_command_success(void);
    extern void test_unpair_command_failure(void);
    extern void test_unpair_command_not_found(void);
    extern void test_unpair_all_command(void);
    extern void test_unpair_all_command_failure(void);
    extern void test_status_command(void);
    extern void test_status_command_streaming_info_unavailable(void);  /* CODE_REVIEW8 Task B */
    extern void test_reset_command(void);

    // New tests for mute/unmute and unpair_all
    RUN_TEST(test_mute_unmute_command);
    RUN_TEST(test_unpair_command_success);
    RUN_TEST(test_unpair_command_failure);
    RUN_TEST(test_unpair_command_not_found);
    RUN_TEST(test_unpair_all_command);
    RUN_TEST(test_unpair_all_command_failure);
    
    // New tests for PAIRED and SAMPLE_RATE
    extern void test_paired_command(void);
    extern void test_sample_rate_command(void);
    RUN_TEST(test_paired_command);
    RUN_TEST(test_sample_rate_command);
    RUN_TEST(test_status_command);
    RUN_TEST(test_status_command_streaming_info_unavailable);  /* CODE_REVIEW8 Task B */
    RUN_TEST(test_reset_command);
    
    // Test DISCONNECT command
    extern void test_disconnect_command(void);
    RUN_TEST(test_disconnect_command);
    
    // Test START and STOP commands
    extern void test_start_command(void);
    extern void test_start_command_stops_beep_and_enables_i2s(void);
    extern void test_stop_command(void);
    RUN_TEST(test_start_command);
    RUN_TEST(test_start_command_stops_beep_and_enables_i2s);
    RUN_TEST(test_stop_command);

    // Negative-path failure simulations
    extern void test_disconnect_failure_command(void);
    extern void test_start_failure_command(void);
    extern void test_stop_failure_command(void);
    RUN_TEST(test_disconnect_failure_command);
    RUN_TEST(test_start_failure_command);
    RUN_TEST(test_stop_failure_command);
    
    // Tests for BEEP command
    extern void test_beep_command_not_connected(void);
    extern void test_beep_command_connected(void);
    extern void test_beep_command_allowed_when_i2s_active(void);
    extern void test_beep_command_busy_when_beep_active(void);
    RUN_TEST(test_beep_command_not_connected);
    RUN_TEST(test_beep_command_connected);
    RUN_TEST(test_beep_command_allowed_when_i2s_active);
    RUN_TEST(test_beep_command_busy_when_beep_active);
    
    // Tests for SYNTH command (host-mode verifies parsing + response)
    extern void test_synth_on_command(void);
    extern void test_synth_off_command(void);
    RUN_TEST(test_synth_on_command);
    RUN_TEST(test_synth_off_command);

    extern void test_diag_i2s_stop_clears_i2s_flag(void);
    RUN_TEST(test_diag_i2s_stop_clears_i2s_flag);

    /* TEST-8: cmd_status_to_name() completeness */
    RUN_TEST(test_status_name_cmd_success);
    RUN_TEST(test_status_name_cmd_error_init_failed);
    RUN_TEST(test_status_name_cmd_error_invalid_param);
    RUN_TEST(test_status_name_cmd_error_unknown);
    RUN_TEST(test_status_name_cmd_error_not_initialized);
    RUN_TEST(test_status_name_cmd_error_too_many_params);
    RUN_TEST(test_status_name_out_of_range_returns_non_null);

    return UNITY_END();
}

// Test mute and unmute flow

// Test UNPAIR removes entry and reports success

// Test UNPAIR surfaces backend failure without mutating storage

// Test UNPAIR reports NOT_FOUND when storage lacks the entry

// Test UNPAIR_ALL clears stored paired devices


// Test listing paired devices

// Test setting sample rate

// Test STATUS returns key fields

/* CODE_REVIEW8 Task B: Test STATUS handles bt_get_streaming_info failure gracefully */

// Test RESET in host-mode returns a mock reboot response

// Test DISCONNECT command behavior

// Test START command behavior

/* I2S must override ongoing BEEP. When a beep is active, START should
 * clear it and leave I2S running. */

// Test STOP command behavior

// Negative-path: simulate backend failure for DISCONNECT

// Negative-path: simulate backend failure for START

// Negative-path: simulate backend failure for STOP

// Test BEEP when not connected should return an error

// Test BEEP when connected should call the audio API and return OK


// Verify SYNTH ON toggles the mode (host: verifies response emitted)

// Verify SYNTH OFF toggles the mode (host: verifies response emitted)

