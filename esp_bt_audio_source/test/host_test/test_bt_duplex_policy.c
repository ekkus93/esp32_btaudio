#include <string.h>

#include "unity.h"

#include "bt_duplex_policy.h"
#include "bt_duplex_policy_manager_stub.h"
#include "bt_duplex_state.h"
#include "bt_hfp_event_command_stub.h"

#define PEER "11:22:33:44:55:66"

static bool s_state_initialized;

void setUp(void)
{
    bt_hfp_event_command_stub_reset();
    bt_duplex_policy_manager_stub_reset(BT_DUPLEX_MODE_AUTO);
    bt_manager_hfp_policy_runtime_reset();
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_state_init());
    s_state_initialized = true;
}

void tearDown(void)
{
    if (s_state_initialized) {
        bt_duplex_state_deinit();
        s_state_initialized = false;
    }
}

static bt_duplex_policy_input_t ready_input(bt_duplex_mode_t mode)
{
    const bt_duplex_policy_input_t input = {
        .requested = mode,
        .current_effective = mode == BT_DUPLEX_MODE_AUTO
            ? BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC : mode,
        .a2dp_connected = true,
        .a2dp_streaming = true,
        .hfp_slc_connected = true,
        .sco_connected = true,
        .a2dp_interrupted_during_sco = false,
        .interruption_reason = BT_DUPLEX_POLICY_REASON_REQUESTED_MODE,
    };
    return input;
}

static uint32_t begin_session(bt_duplex_mode_t configured_mode)
{
    bt_duplex_policy_manager_stub_reset(configured_mode);
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_handle_a2dp_profile_event(
        PEER, BT_A2DP_PROFILE_CONNECTED));
    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_TRUE(snapshot.peer_valid);
    return snapshot.session_generation;
}

static void start_a2dp(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_handle_a2dp_audio_event(
        PEER, BT_A2DP_AUDIO_STARTED));
}

static void connect_hfp(uint32_t generation)
{
    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_profile_state(
        generation, PEER, BT_HFP_PROFILE_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_policy_note_hfp_profile_transition(
        generation, PEER, snapshot.hfp_profile_state,
        BT_HFP_PROFILE_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_profile_state(
        generation, PEER, BT_HFP_PROFILE_SLC_CONNECTED));
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_policy_note_hfp_profile_transition(
        generation, PEER, snapshot.hfp_profile_state,
        BT_HFP_PROFILE_SLC_CONNECTED));
}

static void connect_sco(uint32_t generation)
{
    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_audio_state(
        generation, PEER, BT_HFP_AUDIO_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_policy_note_hfp_audio_transition(
        generation, PEER, snapshot.hfp_audio_state,
        BT_HFP_AUDIO_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_audio_state(
        generation, PEER, BT_HFP_AUDIO_CONNECTED_CVSD));
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_policy_note_hfp_audio_transition(
        generation, PEER, snapshot.hfp_audio_state,
        BT_HFP_AUDIO_CONNECTED_CVSD));
}

static void prepare_active(bt_duplex_mode_t mode, uint32_t *generation_out)
{
    uint32_t generation = begin_session(mode);
    start_a2dp();
    connect_hfp(generation);
    connect_sco(generation);
    bt_hfp_event_command_stub_reset();
    *generation_out = generation;
}

static bool captured_contains(const char *needle)
{
    for (size_t index = 0U; index < bt_hfp_event_command_stub_count(); ++index) {
        const char *line = bt_hfp_event_command_stub_line(index);
        if (line != NULL && strstr(line, needle) != NULL) return true;
    }
    return false;
}

void test_policy_rejects_invalid_arguments(void)
{
    bt_duplex_policy_result_t result;
    bt_duplex_policy_input_t input = ready_input(BT_DUPLEX_MODE_AUTO);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      bt_duplex_policy_evaluate(NULL, &result));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      bt_duplex_policy_evaluate(&input, NULL));
    input.requested = BT_DUPLEX_MODE_COUNT;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      bt_duplex_policy_evaluate(&input, &result));
}

void test_disabled_preserves_a2dp_owner_without_hfp_request(void)
{
    bt_duplex_policy_input_t input = ready_input(BT_DUPLEX_MODE_DISABLED);
    bt_duplex_policy_result_t result;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_policy_evaluate(&input, &result));
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_DISABLED, result.effective);
    TEST_ASSERT_EQUAL(BT_DUPLEX_POLICY_SATISFIED, result.state);
    TEST_ASSERT_EQUAL(BT_DUPLEX_DOWNLINK_OWNER_A2DP,
                      result.downlink_owner);
    TEST_ASSERT_FALSE(result.request_hfp_downlink);
}

void test_a2dp_mic_waits_for_each_required_resource(void)
{
    bt_duplex_policy_input_t input = ready_input(
        BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC);
    bt_duplex_policy_result_t result;

    input.a2dp_connected = false;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_policy_evaluate(&input, &result));
    TEST_ASSERT_EQUAL(BT_DUPLEX_POLICY_REASON_WAITING_A2DP_CONNECTION,
                      result.reason);

    input.a2dp_connected = true;
    input.a2dp_streaming = false;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_policy_evaluate(&input, &result));
    TEST_ASSERT_EQUAL(BT_DUPLEX_POLICY_REASON_WAITING_A2DP_STREAM,
                      result.reason);

    input.a2dp_streaming = true;
    input.hfp_slc_connected = false;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_policy_evaluate(&input, &result));
    TEST_ASSERT_EQUAL(BT_DUPLEX_POLICY_REASON_WAITING_HFP_SLC,
                      result.reason);

    input.hfp_slc_connected = true;
    input.sco_connected = false;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_policy_evaluate(&input, &result));
    TEST_ASSERT_EQUAL(BT_DUPLEX_POLICY_REASON_WAITING_SCO, result.reason);
}

void test_hfp_full_reserves_hfp_downlink(void)
{
    bt_duplex_policy_input_t input = ready_input(
        BT_DUPLEX_MODE_HFP_FULL_DUPLEX);
    bt_duplex_policy_result_t result;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_policy_evaluate(&input, &result));
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_HFP_FULL_DUPLEX, result.effective);
    TEST_ASSERT_EQUAL(BT_DUPLEX_DOWNLINK_OWNER_HFP, result.downlink_owner);
    TEST_ASSERT_TRUE(result.request_hfp_downlink);
}

void test_all_state_permutations_have_exactly_one_downlink_owner(void)
{
    for (int mode = BT_DUPLEX_MODE_DISABLED; mode < BT_DUPLEX_MODE_COUNT;
         ++mode) {
        for (unsigned bits = 0U; bits < 32U; ++bits) {
            bt_duplex_policy_input_t input = ready_input(
                (bt_duplex_mode_t)mode);
            input.a2dp_connected = (bits & 1U) != 0U;
            input.a2dp_streaming = (bits & 2U) != 0U;
            input.hfp_slc_connected = (bits & 4U) != 0U;
            input.sco_connected = (bits & 8U) != 0U;
            input.a2dp_interrupted_during_sco = (bits & 16U) != 0U;
            input.interruption_reason =
                BT_DUPLEX_POLICY_REASON_REMOTE_SUSPENDED_A2DP_DURING_SCO;
            bt_duplex_policy_result_t result;
            TEST_ASSERT_EQUAL(ESP_OK,
                              bt_duplex_policy_evaluate(&input, &result));
            TEST_ASSERT_TRUE(result.downlink_owner ==
                                 BT_DUPLEX_DOWNLINK_OWNER_A2DP ||
                             result.downlink_owner ==
                                 BT_DUPLEX_DOWNLINK_OWNER_HFP);
            TEST_ASSERT_EQUAL(result.downlink_owner ==
                                  BT_DUPLEX_DOWNLINK_OWNER_HFP,
                              result.request_hfp_downlink);
        }
    }
}

void test_strict_a2dp_mic_reports_incompatibility_without_mode_change(void)
{
    uint32_t generation;
    prepare_active(BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC, &generation);
    TEST_ASSERT_NOT_EQUAL(0U, generation);

    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_handle_a2dp_audio_event(
        PEER, BT_A2DP_AUDIO_REMOTE_SUSPENDED));

    bt_duplex_policy_snapshot_t policy;
    bt_duplex_snapshot_t duplex;
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_get_policy_snapshot(&policy));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(BT_DUPLEX_POLICY_INCOMPATIBLE, policy.state);
    TEST_ASSERT_EQUAL(
        BT_DUPLEX_POLICY_REASON_REMOTE_SUSPENDED_A2DP_DURING_SCO,
        policy.reason);
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC, duplex.effective_mode);
    TEST_ASSERT_EQUAL_size_t(0U, bt_hfp_event_command_stub_count());
}

void test_auto_remote_suspend_switches_effective_mode_once(void)
{
    uint32_t generation;
    prepare_active(BT_DUPLEX_MODE_AUTO, &generation);

    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_handle_a2dp_audio_event(
        PEER, BT_A2DP_AUDIO_REMOTE_SUSPENDED));
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_handle_a2dp_audio_event(
        PEER, BT_A2DP_AUDIO_REMOTE_SUSPENDED));

    bt_duplex_policy_snapshot_t policy;
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_get_policy_snapshot(&policy));
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_HFP_FULL_DUPLEX, policy.effective);
    TEST_ASSERT_EQUAL(BT_DUPLEX_POLICY_COMPATIBILITY_REQUIRED, policy.state);
    TEST_ASSERT_EQUAL(BT_DUPLEX_DOWNLINK_OWNER_HFP, policy.downlink_owner);
    TEST_ASSERT_TRUE(policy.request_hfp_downlink);
    TEST_ASSERT_EQUAL_UINT64(1U, policy.a2dp_interruptions_lifetime);
    TEST_ASSERT_EQUAL_UINT64(1U, policy.effective_mode_changes_lifetime);
    TEST_ASSERT_EQUAL_size_t(1U, bt_hfp_event_command_stub_count());

    char expected[160];
    (void)snprintf(expected, sizeof(expected),
                   "EVENT|HFP|MODE|A2DP_MIC|HFP_FULL|"
                   "REMOTE_SUSPENDED_A2DP_DURING_SCO|%" PRIu32,
                   generation);
    TEST_ASSERT_EQUAL_STRING(expected, bt_hfp_event_command_stub_line(0U));
}

void test_auto_a2dp_resume_restores_preferred_mode(void)
{
    uint32_t generation;
    prepare_active(BT_DUPLEX_MODE_AUTO, &generation);
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_handle_a2dp_audio_event(
        PEER, BT_A2DP_AUDIO_REMOTE_SUSPENDED));
    bt_hfp_event_command_stub_reset();

    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_handle_a2dp_audio_event(
        PEER, BT_A2DP_AUDIO_STARTED));

    bt_duplex_policy_snapshot_t policy;
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_get_policy_snapshot(&policy));
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC, policy.effective);
    TEST_ASSERT_EQUAL(BT_DUPLEX_POLICY_REASON_A2DP_RESUMED, policy.reason);
    TEST_ASSERT_TRUE(captured_contains("|HFP_FULL|A2DP_MIC|A2DP_RESUMED|"));
}

void test_auto_sco_stop_restores_preferred_mode(void)
{
    uint32_t generation;
    prepare_active(BT_DUPLEX_MODE_AUTO, &generation);
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_handle_a2dp_audio_event(
        PEER, BT_A2DP_AUDIO_REMOTE_SUSPENDED));
    bt_hfp_event_command_stub_reset();

    bt_duplex_snapshot_t before;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&before));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_audio_state(
        generation, PEER, BT_HFP_AUDIO_DISCONNECTED));
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_policy_note_hfp_audio_transition(
        generation, PEER, before.hfp_audio_state,
        BT_HFP_AUDIO_DISCONNECTED));

    bt_duplex_policy_snapshot_t policy;
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_hfp_get_policy_snapshot(&policy));
    TEST_ASSERT_EQUAL(BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC, policy.effective);
    TEST_ASSERT_EQUAL(BT_DUPLEX_POLICY_REASON_SCO_STOPPED, policy.reason);
    TEST_ASSERT_TRUE(captured_contains("|HFP_FULL|A2DP_MIC|SCO_STOPPED|"));
}

void test_stale_generation_policy_event_is_rejected(void)
{
    uint32_t old_generation = begin_session(BT_DUPLEX_MODE_AUTO);
    uint32_t new_generation = 0U;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_session_begin(
        PEER, BT_DUPLEX_MODE_AUTO, &new_generation));
    TEST_ASSERT_NOT_EQUAL(old_generation, new_generation);
    bt_hfp_event_command_stub_reset();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_manager_hfp_policy_note_hfp_audio_transition(
                          old_generation, PEER,
                          BT_HFP_AUDIO_CONNECTING,
                          BT_HFP_AUDIO_CONNECTED_CVSD));
    TEST_ASSERT_EQUAL_size_t(0U, bt_hfp_event_command_stub_count());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_policy_rejects_invalid_arguments);
    RUN_TEST(test_disabled_preserves_a2dp_owner_without_hfp_request);
    RUN_TEST(test_a2dp_mic_waits_for_each_required_resource);
    RUN_TEST(test_hfp_full_reserves_hfp_downlink);
    RUN_TEST(test_all_state_permutations_have_exactly_one_downlink_owner);
    RUN_TEST(test_strict_a2dp_mic_reports_incompatibility_without_mode_change);
    RUN_TEST(test_auto_remote_suspend_switches_effective_mode_once);
    RUN_TEST(test_auto_a2dp_resume_restores_preferred_mode);
    RUN_TEST(test_auto_sco_stop_restores_preferred_mode);
    RUN_TEST(test_stale_generation_policy_event_is_rejected);
    return UNITY_END();
}
