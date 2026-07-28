#include "unity.h"

#include <string.h>

#include "bt_hfp_manager_command_stub.h"
#include "command_interface.h"
#include "mock_uart.h"

static const char *execute_fd16_status(void)
{
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("HFP STATUS", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    return mock_uart_get_tx_data();
}

void test_hfp_status_reports_fd16_policy_snapshot(void)
{
    bt_hfp_manager_status_t status;
    memset(&status, 0, sizeof(status));
    status.manager_initialized = true;
    status.configured_mode = BT_DUPLEX_MODE_AUTO;
    status.duplex.session_generation = 77U;
    status.duplex.requested_mode = BT_DUPLEX_MODE_AUTO;
    status.duplex.effective_mode = BT_DUPLEX_MODE_HFP_FULL_DUPLEX;
    status.policy.initialized = true;
    status.policy.generation = 77U;
    status.policy.requested = BT_DUPLEX_MODE_AUTO;
    status.policy.effective = BT_DUPLEX_MODE_HFP_FULL_DUPLEX;
    status.policy.state = BT_DUPLEX_POLICY_COMPATIBILITY_REQUIRED;
    status.policy.reason =
        BT_DUPLEX_POLICY_REASON_REMOTE_SUSPENDED_A2DP_DURING_SCO;
    status.policy.downlink_owner = BT_DUPLEX_DOWNLINK_OWNER_HFP;
    status.policy.request_hfp_downlink = true;
    mock_bt_hfp_manager_set_status(&status, ESP_OK);

    const char *tx = execute_fd16_status();
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_manager_status_calls());
    TEST_ASSERT_NOT_NULL(strstr(
        tx,
        "INFO|HFP|STATUS_POLICY|STATE=COMPATIBILITY_REQUIRED,"
        "REASON=REMOTE_SUSPENDED_A2DP_DURING_SCO,REQUESTED=AUTO,"
        "EFFECTIVE=HFP_FULL,DOWNLINK_OWNER=HFP,"
        "HFP_DOWNLINK_REQUESTED=1,GEN=77"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|HFP|STATUS|GEN=77"));
}

void test_hfp_status_reports_unavailable_policy_without_fake_zero(void)
{
    bt_hfp_manager_status_t status;
    memset(&status, 0, sizeof(status));
    status.manager_initialized = true;
    status.configured_mode = BT_DUPLEX_MODE_DISABLED;
    mock_bt_hfp_manager_set_status(&status, ESP_OK);

    const char *tx = execute_fd16_status();
    TEST_ASSERT_NOT_NULL(strstr(
        tx,
        "INFO|HFP|STATUS_POLICY|STATE=UNAVAILABLE,REASON=NONE,"
        "REQUESTED=NONE,EFFECTIVE=NONE,DOWNLINK_OWNER=NONE,"
        "HFP_DOWNLINK_REQUESTED=0,GEN=0"));
    TEST_ASSERT_NULL(strstr(tx, "STATE=SATISFIED,REASON=REQUESTED_MODE"));
}
