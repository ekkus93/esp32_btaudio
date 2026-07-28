#include "unity.h"

#include "bt_duplex_policy.h"

void setUp(void) {}
void tearDown(void) {}

typedef enum {
    SIGNAL_A2DP_STREAM = 0,
    SIGNAL_HFP_SLC,
    SIGNAL_SCO,
} policy_signal_t;

static void apply_signal(bt_duplex_policy_input_t *input,
                         policy_signal_t signal)
{
    switch (signal) {
    case SIGNAL_A2DP_STREAM:
        input->a2dp_streaming = true;
        break;
    case SIGNAL_HFP_SLC:
        input->hfp_slc_connected = true;
        break;
    case SIGNAL_SCO:
        input->sco_connected = true;
        break;
    default:
        TEST_FAIL_MESSAGE("invalid ordering signal");
    }
}

void test_auto_prerequisite_orderings_converge_without_dual_owner(void)
{
    static const policy_signal_t permutations[][3] = {
        {SIGNAL_A2DP_STREAM, SIGNAL_HFP_SLC, SIGNAL_SCO},
        {SIGNAL_A2DP_STREAM, SIGNAL_SCO, SIGNAL_HFP_SLC},
        {SIGNAL_HFP_SLC, SIGNAL_A2DP_STREAM, SIGNAL_SCO},
        {SIGNAL_HFP_SLC, SIGNAL_SCO, SIGNAL_A2DP_STREAM},
        {SIGNAL_SCO, SIGNAL_A2DP_STREAM, SIGNAL_HFP_SLC},
        {SIGNAL_SCO, SIGNAL_HFP_SLC, SIGNAL_A2DP_STREAM},
    };

    for (size_t ordering = 0U;
         ordering < sizeof(permutations) / sizeof(permutations[0]);
         ++ordering) {
        bt_duplex_policy_input_t input = {
            .requested = BT_DUPLEX_MODE_AUTO,
            .current_effective = BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC,
            .a2dp_connected = true,
            .a2dp_streaming = false,
            .hfp_slc_connected = false,
            .sco_connected = false,
            .a2dp_interrupted_during_sco = false,
            .interruption_reason = BT_DUPLEX_POLICY_REASON_REQUESTED_MODE,
        };

        for (size_t step = 0U; step < 3U; ++step) {
            apply_signal(&input, permutations[ordering][step]);
            bt_duplex_policy_result_t result;
            TEST_ASSERT_EQUAL(ESP_OK,
                              bt_duplex_policy_evaluate(&input, &result));
            TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC,
                              result.effective);
            TEST_ASSERT_EQUAL(BT_DUPLEX_DOWNLINK_OWNER_A2DP,
                              result.downlink_owner);
            TEST_ASSERT_FALSE(result.request_hfp_downlink);
            if (step == 2U) {
                TEST_ASSERT_EQUAL(BT_DUPLEX_POLICY_SATISFIED,
                                  result.state);
            } else {
                TEST_ASSERT_EQUAL(BT_DUPLEX_POLICY_WAITING, result.state);
            }
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_auto_prerequisite_orderings_converge_without_dual_owner);
    return UNITY_END();
}
