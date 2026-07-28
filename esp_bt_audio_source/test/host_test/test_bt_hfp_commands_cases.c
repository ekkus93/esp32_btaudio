#include "unity.h"

#include <stdint.h>
#include <string.h>

#include "bt_hfp_manager_command_stub.h"
#include "command_interface.h"
#include "mock_uart.h"

#define PEER "AA:BB:CC:DD:EE:FF"

static const char *execute_command(const char *command)
{
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse(command, &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    return mock_uart_get_tx_data();
}

static bt_hfp_manager_status_t connected_status(void)
{
    bt_hfp_manager_status_t status;
    memset(&status, 0, sizeof(status));
    status.manager_initialized = true;
    status.configured_mode = BT_DUPLEX_MODE_AUTO;
    status.duplex.peer_valid = true;
    strcpy(status.duplex.peer_mac, PEER);
    status.duplex.session_generation = 77U;
    status.duplex.requested_mode = BT_DUPLEX_MODE_AUTO;
    status.duplex.effective_mode = BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC;
    status.duplex.a2dp_profile_state = BT_A2DP_PROFILE_CONNECTED;
    status.duplex.a2dp_audio_state = BT_A2DP_AUDIO_STARTED;
    status.duplex.hfp_profile_state = BT_HFP_PROFILE_SLC_CONNECTED;
    status.duplex.hfp_audio_state = BT_HFP_AUDIO_CONNECTED_CVSD;
    status.duplex.codec = BT_HFP_CODEC_CVSD;
    status.duplex.i2s_state = BT_HFP_I2S_RUNNING;
    status.duplex.health = BT_AUDIO_HEALTH_DEGRADED;
    status.duplex.last_error = ESP_ERR_TIMEOUT;
    strcpy(status.duplex.last_error_text, "timed|out,visible\nnow");
    return status;
}

void setUp(void)
{
    mock_uart_init(115200);
    mock_uart_reset_tx();
    mock_bt_hfp_manager_reset();
    cmd_test_reset_cmd_process_state();
}

void tearDown(void) {}

void test_hfp_parser_accepts_all_valid_forms(void)
{
    const char *commands[] = {
        "HFP STATUS", "HFP CONNECT AA:BB:CC:DD:EE:FF",
        "HFP DISCONNECT", "HFP AUDIO START", "HFP AUDIO STOP",
        "HFP MODE DISABLED", "HFP MODE A2DP_MIC",
        "HFP MODE HFP_FULL", "HFP MODE AUTO", "HFP CODEC",
        "HFP STATS", "HFP RESETSTATS",
    };
    for (size_t index = 0U; index < sizeof(commands) / sizeof(commands[0]);
         ++index) {
        cmd_context_t ctx;
        TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse(commands[index], &ctx));
        TEST_ASSERT_EQUAL(CMD_TYPE_HFP, ctx.type);
        TEST_ASSERT_GREATER_THAN_UINT(0U, (unsigned)ctx.param_count);
    }
}

void test_hfp_status_uses_one_consistent_snapshot_and_sanitizes_text(void)
{
    bt_hfp_manager_status_t status = connected_status();
    mock_bt_hfp_manager_set_status(&status, ESP_OK);
    const char *tx = execute_command("HFP STATUS");

    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_manager_status_calls());
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|HFP|STATUS|GEN=77"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "PEER=" PEER));
    TEST_ASSERT_NOT_NULL(strstr(tx, "CONFIG_MODE=AUTO"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "HFP_PROFILE=SLC_CONNECTED"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "HFP_AUDIO=CONNECTED_CVSD"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERROR_TEXT=timed_out_visible_now"));
    TEST_ASSERT_NULL(strstr(tx, "RESPONSE_TOO_LONG"));
}

void test_hfp_connect_reports_acceptance_not_completion(void)
{
    const char *tx = execute_command("HFP CONNECT " PEER);
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_manager_connect_calls());
    TEST_ASSERT_EQUAL_STRING(PEER, mock_bt_hfp_manager_last_connect_mac());
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|HFP|CONNECT_ACCEPTED|"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "COMPLETION=HFP_PROFILE_EVENT"));
    TEST_ASSERT_NULL(strstr(tx, "|CONNECTED|"));
}

void test_hfp_connect_already_connected_is_explicit(void)
{
    bt_hfp_manager_status_t status = connected_status();
    mock_bt_hfp_manager_set_status(&status, ESP_OK);
    const char *tx = execute_command("HFP CONNECT " PEER);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|HFP|ALREADY_CONNECTED|"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "GEN=77"));
}

void test_hfp_connect_exact_backend_error_is_reported(void)
{
    mock_bt_hfp_manager_set_connect_result(ESP_ERR_INVALID_STATE);
    const char *tx = execute_command("HFP CONNECT " PEER);
    TEST_ASSERT_NOT_NULL(strstr(
        tx, "ERR|HFP|ESP_ERR_INVALID_STATE|" PEER));
    TEST_ASSERT_NULL(strstr(tx, "CONNECT_ACCEPTED"));
}

void test_hfp_disconnect_acceptance_and_idempotence_are_distinct(void)
{
    bt_hfp_manager_status_t status = connected_status();
    mock_bt_hfp_manager_set_status(&status, ESP_OK);
    const char *tx = execute_command("HFP DISCONNECT");
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|HFP|DISCONNECT_ACCEPTED|"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "COMPLETION=HFP_PROFILE_EVENT"));

    mock_bt_hfp_manager_reset();
    tx = execute_command("HFP DISCONNECT");
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|HFP|ALREADY_DISCONNECTED|"));
}

void test_hfp_audio_start_stop_only_claim_confirmed_success(void)
{
    bt_hfp_manager_status_t status = connected_status();
    mock_bt_hfp_manager_set_status(&status, ESP_OK);
    const char *tx = execute_command("HFP AUDIO START");
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_manager_audio_start_calls());
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|HFP|AUDIO_STARTED|GEN=77,CODEC=CVSD"));

    tx = execute_command("HFP AUDIO STOP");
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_manager_audio_stop_calls());
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|HFP|AUDIO_STOPPED|GEN=77"));

    mock_bt_hfp_manager_set_audio_start_result(ESP_ERR_TIMEOUT);
    tx = execute_command("HFP AUDIO START");
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|HFP|ESP_ERR_TIMEOUT|"));
    TEST_ASSERT_NULL(strstr(tx, "AUDIO_STARTED"));
}

void test_hfp_mode_parsing_is_exact(void)
{
    struct {
        const char *command;
        bt_duplex_mode_t expected;
        const char *wire;
    } cases[] = {
        {"HFP MODE DISABLED", BT_DUPLEX_MODE_DISABLED, "DISABLED"},
        {"HFP MODE A2DP_MIC", BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC, "A2DP_MIC"},
        {"HFP MODE HFP_FULL", BT_DUPLEX_MODE_HFP_FULL_DUPLEX, "HFP_FULL"},
        {"HFP MODE AUTO", BT_DUPLEX_MODE_AUTO, "AUTO"},
    };

    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]);
         ++index) {
        mock_bt_hfp_manager_reset();
        const char *tx = execute_command(cases[index].command);
        TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_manager_mode_calls());
        TEST_ASSERT_EQUAL(cases[index].expected,
                          mock_bt_hfp_manager_last_mode());
        char expected[64];
        snprintf(expected, sizeof(expected), "CONFIG_MODE=%s", cases[index].wire);
        TEST_ASSERT_NOT_NULL(strstr(tx, expected));
    }

    mock_bt_hfp_manager_reset();
    const char *tx = execute_command("HFP MODE A2DP");
    TEST_ASSERT_EQUAL_UINT(0U, mock_bt_hfp_manager_mode_calls());
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|HFP|UNKNOWN_MODE|A2DP"));
}

void test_hfp_codec_reports_authoritative_generation(void)
{
    bt_hfp_manager_status_t status = connected_status();
    mock_bt_hfp_manager_set_status(&status, ESP_OK);
    const char *tx = execute_command("HFP CODEC");
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_manager_status_calls());
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|HFP|CODEC|GEN=77,CODEC=CVSD"));
}

void test_hfp_stats_max_values_are_split_without_truncation(void)
{
    bt_hfp_manager_stats_t stats;
    memset(&stats, 0xFF, sizeof(stats));
    stats.peer_valid = true;
    memcpy(stats.peer_mac, PEER, sizeof(PEER));
    mock_bt_hfp_manager_set_stats(&stats, ESP_OK);

    const char *tx = execute_command("HFP STATS");
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_manager_stats_calls());
    TEST_ASSERT_NOT_NULL(strstr(tx, "INFO|HFP|STATS_STATE|"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "INFO|HFP|STATS_I2S5|"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|HFP|STATS|"));
    TEST_ASSERT_NULL(strstr(tx, "RESPONSE_TOO_LONG"));
    TEST_ASSERT_NULL(strstr(tx, "STATS_LINE_TOO_LONG"));
}

void test_hfp_resetstats_reports_exact_success_or_failure(void)
{
    const char *tx = execute_command("HFP RESETSTATS");
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_manager_reset_stats_calls());
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|HFP|STATS_RESET|"));

    mock_bt_hfp_manager_set_reset_stats_result(ESP_ERR_INVALID_STATE);
    tx = execute_command("HFP RESETSTATS");
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|HFP|ESP_ERR_INVALID_STATE|"));
    TEST_ASSERT_NULL(strstr(tx, "STATS_RESET"));
}

void test_hfp_invalid_forms_are_explicit(void)
{
    const char *commands[] = {
        "HFP", "HFP STATUS EXTRA", "HFP CONNECT",
        "HFP DISCONNECT EXTRA", "HFP AUDIO", "HFP AUDIO PAUSE",
        "HFP MODE", "HFP CODEC EXTRA", "HFP STATS EXTRA",
        "HFP RESETSTATS EXTRA", "HFP UNKNOWN",
    };
    for (size_t index = 0U; index < sizeof(commands) / sizeof(commands[0]);
         ++index) {
        const char *tx = execute_command(commands[index]);
        TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|HFP|"));
    }
}

void test_response_overflow_fails_closed_without_buffer_overread(void)
{
    char data[700];
    memset(data, 'X', sizeof(data) - 1U);
    data[sizeof(data) - 1U] = '\0';
    mock_uart_reset_tx();
    TEST_ASSERT_EQUAL(CMD_ERROR_TOO_MANY_PARAMS,
                      cmd_send_response(CMD_STATUS_OK, "HFP", "TEST", data));
    TEST_ASSERT_EQUAL_STRING("ERR|HFP|RESPONSE_TOO_LONG|\r\n",
                             mock_uart_get_tx_data());
}
