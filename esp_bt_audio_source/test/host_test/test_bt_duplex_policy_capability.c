#include <inttypes.h>
#include <stdio.h>

#include "unity.h"

#include "bt_duplex_policy.h"
#include "bt_hfp_event_command_stub.h"
#include "bt_hfp_event_contract.h"

void setUp(void)
{
    bt_hfp_event_command_stub_reset();
}

void tearDown(void) {}

void test_hfp_full_never_claims_unimplemented_downlink(void)
{
    const bt_duplex_policy_input_t input = {
        .requested = BT_DUPLEX_MODE_HFP_FULL_DUPLEX,
        .current_effective = BT_DUPLEX_MODE_HFP_FULL_DUPLEX,
        .a2dp_connected = true,
        .a2dp_streaming = true,
        .hfp_slc_connected = true,
        .sco_connected = true,
        .a2dp_interrupted_during_sco = false,
        .interruption_reason = BT_DUPLEX_POLICY_REASON_REQUESTED_MODE,
    };
    bt_duplex_policy_result_t result;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_policy_evaluate(&input, &result));
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_HFP_FULL_DUPLEX, result.effective);
    TEST_ASSERT_EQUAL(BT_DUPLEX_POLICY_COMPATIBILITY_REQUIRED, result.state);
    TEST_ASSERT_EQUAL(BT_DUPLEX_POLICY_REASON_HFP_DOWNLINK_NOT_IMPLEMENTED,
                      result.reason);
    TEST_ASSERT_EQUAL(BT_DUPLEX_DOWNLINK_OWNER_HFP, result.downlink_owner);
    TEST_ASSERT_TRUE(result.request_hfp_downlink);

    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_event_emit_policy(
        result.state, result.reason, input.requested, result.effective,
        result.downlink_owner, 17U));
    TEST_ASSERT_EQUAL_size_t(1U, bt_hfp_event_command_stub_count());
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|POLICY|COMPATIBILITY_REQUIRED|"
        "HFP_DOWNLINK_NOT_IMPLEMENTED|HFP_FULL|HFP_FULL|HFP|17",
        bt_hfp_event_command_stub_line(0U));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hfp_full_never_claims_unimplemented_downlink);
    return UNITY_END();
}
