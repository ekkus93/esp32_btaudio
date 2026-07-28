#include "unity.h"

#include <string.h>

#include "bt_hfp_manager_command_stub.h"
#include "command_interface.h"
#include "mock_uart.h"

#define PEER "AA:BB:CC:DD:EE:FF"

static const char *run_hfp(const char *command)
{
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse(command, &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    return mock_uart_get_tx_data();
}

void test_hfp_connect_invalid_mac_preserves_exact_backend_error(void)
{
    mock_bt_hfp_manager_set_connect_result(ESP_ERR_INVALID_ARG);
    const char *tx = run_hfp("HFP CONNECT AA-BB-CC-DD-EE-FF");
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|HFP|ESP_ERR_INVALID_ARG|"));
    TEST_ASSERT_NULL(strstr(tx, "CONNECT_ACCEPTED"));
}

void test_hfp_connect_pre_status_failure_does_not_block_accepted_request(void)
{
    mock_bt_hfp_manager_set_status(NULL, ESP_ERR_TIMEOUT);
    const char *tx = run_hfp("HFP CONNECT " PEER);
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_manager_connect_calls());
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|HFP|CONNECT_ACCEPTED|"));
    TEST_ASSERT_NULL(strstr(tx, "ERR|HFP|ESP_ERR_TIMEOUT|"));
}

void test_hfp_audio_success_is_not_retracted_by_status_failure(void)
{
    mock_bt_hfp_manager_set_status(NULL, ESP_ERR_TIMEOUT);
    const char *tx = run_hfp("HFP AUDIO START");
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_manager_audio_start_calls());
    TEST_ASSERT_NOT_NULL(strstr(
        tx, "ERR|HFP|AUDIO_STATUS_UNAVAILABLE|OPERATION=START,"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "LOWER_OPERATION=SUCCEEDED"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "STATUS_ERROR=ESP_ERR_TIMEOUT"));
    TEST_ASSERT_NULL(strstr(tx, "OK|HFP|AUDIO_STARTED|"));

    tx = run_hfp("HFP AUDIO STOP");
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_manager_audio_stop_calls());
    TEST_ASSERT_NOT_NULL(strstr(
        tx, "ERR|HFP|AUDIO_STATUS_UNAVAILABLE|OPERATION=STOP,"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "LOWER_OPERATION=SUCCEEDED"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "STATUS_ERROR=ESP_ERR_TIMEOUT"));
    TEST_ASSERT_NULL(strstr(tx, "OK|HFP|AUDIO_STOPPED|"));
}

void test_hfp_mode_success_does_not_require_followup_snapshot(void)
{
    mock_bt_hfp_manager_set_status(NULL, ESP_ERR_TIMEOUT);
    const char *tx = run_hfp("HFP MODE HFP_FULL");
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_manager_mode_calls());
    TEST_ASSERT_EQUAL_UINT(0U, mock_bt_hfp_manager_status_calls());
    TEST_ASSERT_NOT_NULL(strstr(
        tx, "OK|HFP|MODE_SET|CONFIG_MODE=HFP_FULL,EFFECTIVE=HFP_FULL"));
}
