/* test_commands_shared.h — shared decls for the split test_commands
 * executable (runner + grouped test bodies). Not a public header. */
#ifndef TEST_COMMANDS_SHARED_H
#define TEST_COMMANDS_SHARED_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include "unity.h"
#include "command_interface.h"
#include "bt_manager.h"
#include "mock_uart.h"
#include "nvs_storage.h"
#include "audio_processor.h"
#include "esp_log.h"

void bt_manager_test_set_connection_state(int v);
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

/* helper defined (non-static) in test_commands.c */
int count_substring(const char* haystack, const char* needle);

/* test bodies live in test_commands_{parse,audio,bt}.c */
void test_debug_log_sets_level_and_response(void);
void test_parse_scan_command(void);
void test_parse_connect_command(void);
void test_parse_i2s_config_command(void);
void test_parse_i2s_config_command_with_format(void);
void test_parse_invalid_command(void);
void test_parse_malformed_tokens(void);
void test_parse_command_with_whitespace(void);
void test_parse_diag_command(void);
void test_parse_empty_command_should_error(void);
void test_parse_truly_empty_string_should_error(void);
void test_parse_whitespace_only_should_error(void);
void test_parse_limits_param_count_and_truncates(void);
void test_parse_connect_name_preserves_spaces(void);
void test_send_response(void);
void test_command_processing(void);
void test_cmd_process_handles_multiple_commands_in_one_read(void);
void test_cmd_process_accumulates_partial_line_across_calls(void);
void test_cmd_process_recovers_after_overflow_reset(void);
void test_help_command(void);
void test_version_command(void);
void test_diag_command_reports_state(void);
void test_scan_invokes_manager(void);
void test_confirm_pin_command(void);
void test_enter_pin_command(void);
void test_volume_invalid_param_should_error(void);
void test_volume_missing_param_should_error(void);
void test_i2s_config_invalid_rate_should_error(void);
void test_i2s_config_invalid_bit_depth_should_error(void);
void test_i2s_config_invalid_channels_should_error(void);
void test_debug_mock_add_missing_param_errors(void);
void test_status_name_cmd_success(void);
void test_status_name_cmd_error_init_failed(void);
void test_status_name_cmd_error_invalid_param(void);
void test_status_name_cmd_error_unknown(void);
void test_status_name_cmd_error_not_initialized(void);
void test_status_name_cmd_error_too_many_params(void);
void test_status_name_out_of_range_returns_non_null(void);
void test_mute_unmute_command(void);
void test_unpair_command_success(void);
void test_unpair_command_failure(void);
void test_unpair_command_not_found(void);
void test_unpair_all_command(void);
void test_unpair_all_command_failure(void);
void test_paired_command(void);
void test_sample_rate_command(void);
void test_status_command(void);
void test_status_command_streaming_info_unavailable(void);
void test_reset_command(void);
void test_disconnect_command(void);
void test_start_command(void);
void test_start_command_stops_beep_and_enables_i2s(void);
void test_stop_command(void);
void test_disconnect_failure_command(void);
void test_start_failure_command(void);
void test_stop_failure_command(void);
void test_beep_command_not_connected(void);
void test_beep_command_connected(void);
void test_beep_command_allowed_when_i2s_active(void);
void test_beep_command_busy_when_beep_active(void);
void test_synth_on_command(void);
void test_synth_off_command(void);
void test_diag_i2s_stop_clears_i2s_flag(void);

#endif /* TEST_COMMANDS_SHARED_H */
