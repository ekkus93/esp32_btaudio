#include <string.h>

#include "unity.h"

#include "bt_duplex_state.h"
#include "bt_duplex_state_internal.h"
#include "bt_duplex_state_mode.h"
#include "bt_hfp_event_command_stub.h"
#include "bt_hfp_event_contract.h"

#define PEER "11:22:33:aa:bb:cc"

static bool s_state_initialized;

void setUp(void)
{
    bt_hfp_event_command_stub_reset();
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

static uint32_t begin_session(bt_duplex_mode_t mode)
{
    uint32_t generation = 0U;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_duplex_session_begin(PEER, mode, &generation));
    TEST_ASSERT_NOT_EQUAL(0U, generation);
    return generation;
}

void test_profile_event_is_canonical_and_bounded(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_event_emit_profile(
        BT_HFP_PROFILE_CONNECTING, PEER, 7U));
    TEST_ASSERT_EQUAL_size_t(1U, bt_hfp_event_command_stub_count());
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|PROFILE|CONNECTING|11:22:33:AA:BB:CC|7",
        bt_hfp_event_command_stub_line(0U));
}

void test_global_profile_event_uses_none_and_zero_session(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_event_emit_profile(
        BT_HFP_PROFILE_UNINITIALIZED, NULL, 0U));
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|PROFILE|UNINITIALIZED|NONE|0",
        bt_hfp_event_command_stub_line(0U));
}

void test_invalid_profile_mac_is_rejected_without_emission(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, bt_hfp_event_emit_profile(
        BT_HFP_PROFILE_CONNECTING, "11:22:33:44:55|66", 1U));
    TEST_ASSERT_EQUAL_size_t(0U, bt_hfp_event_command_stub_count());
}

void test_audio_mode_i2s_and_health_wire_records_are_stable(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_event_emit_audio(
        BT_HFP_AUDIO_CONNECTED_CVSD, BT_HFP_CODEC_CVSD, 9U));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_event_emit_mode(
        BT_DUPLEX_MODE_AUTO, BT_DUPLEX_MODE_HFP_FULL_DUPLEX,
        BT_HFP_MODE_EVENT_REASON_POLICY, 9U));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_event_emit_i2s(
        BT_HFP_I2S_QUARANTINED, ESP_ERR_TIMEOUT, 9U));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_event_emit_health(
        BT_AUDIO_HEALTH_FAULTED, ESP_ERR_TIMEOUT, 3U, 9U));

    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|AUDIO|CONNECTED_CVSD|CVSD|9",
        bt_hfp_event_command_stub_line(0U));
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|MODE|AUTO|HFP_FULL|POLICY|9",
        bt_hfp_event_command_stub_line(1U));
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|I2S|QUARANTINED|ESP_ERR_TIMEOUT|9",
        bt_hfp_event_command_stub_line(2U));
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|HEALTH|FAULTED|TIMEOUT|3|9",
        bt_hfp_event_command_stub_line(3U));
}

void test_i2s_event_rejects_inconsistent_error_fields(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, bt_hfp_event_emit_i2s(
        BT_HFP_I2S_FAULTED, ESP_OK, 1U));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, bt_hfp_event_emit_i2s(
        BT_HFP_I2S_RUNNING, ESP_FAIL, 1U));
    TEST_ASSERT_EQUAL_size_t(0U, bt_hfp_event_command_stub_count());
}

void test_generic_failure_uses_stable_esp_fail_token(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_event_emit_i2s(
        BT_HFP_I2S_FAULTED, ESP_FAIL, 4U));
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|I2S|FAULTED|ESP_FAIL|4",
        bt_hfp_event_command_stub_line(0U));
}

void test_unknown_error_uses_stable_unknown_token(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_event_emit_i2s(
        BT_HFP_I2S_FAULTED, (esp_err_t)-32000, 4U));
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|I2S|FAULTED|ESP_ERR_UNKNOWN|4",
        bt_hfp_event_command_stub_line(0U));
}

void test_command_backend_error_is_propagated(void)
{
    bt_hfp_event_command_stub_set_status(CMD_ERROR_NOT_INITIALIZED);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bt_hfp_event_emit_profile(
        BT_HFP_PROFILE_DISCONNECTED, PEER, 1U));
}

void test_session_begin_emits_profile_and_mode_once(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    TEST_ASSERT_EQUAL_size_t(2U, bt_hfp_event_command_stub_count());
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|PROFILE|DISCONNECTED|11:22:33:AA:BB:CC|1",
        bt_hfp_event_command_stub_line(0U));
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|MODE|DISABLED|AUTO|SESSION_BEGIN|1",
        bt_hfp_event_command_stub_line(1U));
    TEST_ASSERT_EQUAL_UINT32(1U, generation);
}

void test_duplicate_profile_audio_mode_and_i2s_states_are_suppressed(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    bt_hfp_event_command_stub_reset();

    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_mode_with_reason(
        generation, PEER, BT_DUPLEX_MODE_HFP_FULL_DUPLEX,
        BT_HFP_MODE_EVENT_REASON_COMMAND));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_mode_with_reason(
        generation, PEER, BT_DUPLEX_MODE_HFP_FULL_DUPLEX,
        BT_HFP_MODE_EVENT_REASON_COMMAND));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_profile_state(
        generation, PEER, BT_HFP_PROFILE_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_profile_state(
        generation, PEER, BT_HFP_PROFILE_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_audio_state(
        generation, PEER, BT_HFP_AUDIO_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_audio_state(
        generation, PEER, BT_HFP_AUDIO_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_i2s_state(
        generation, PEER, BT_HFP_I2S_STARTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_i2s_state(
        generation, PEER, BT_HFP_I2S_STARTING));

    TEST_ASSERT_EQUAL_size_t(4U, bt_hfp_event_command_stub_count());
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|MODE|AUTO|HFP_FULL|COMMAND|1",
        bt_hfp_event_command_stub_line(0U));
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|PROFILE|CONNECTING|11:22:33:AA:BB:CC|1",
        bt_hfp_event_command_stub_line(1U));
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|AUDIO|CONNECTING|NONE|1",
        bt_hfp_event_command_stub_line(2U));
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|I2S|STARTING|ESP_OK|1",
        bt_hfp_event_command_stub_line(3U));
}

void test_audio_event_uses_codec_after_authoritative_transition(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    bt_hfp_event_command_stub_reset();
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_audio_state(
        generation, PEER, BT_HFP_AUDIO_CONNECTING));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_hfp_audio_state(
        generation, PEER, BT_HFP_AUDIO_CONNECTED_CVSD));
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|AUDIO|CONNECTED_CVSD|CVSD|1",
        bt_hfp_event_command_stub_line(1U));
}

void test_error_aware_i2s_transition_preserves_exact_error(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    bt_hfp_event_command_stub_reset();
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_i2s_state_with_error(
        generation, PEER, BT_HFP_I2S_FAULTED, ESP_ERR_TIMEOUT));
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|I2S|FAULTED|ESP_ERR_TIMEOUT|1",
        bt_hfp_event_command_stub_line(0U));
}

void test_pcm_and_counter_updates_do_not_emit_per_frame_events(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    bt_hfp_event_command_stub_reset();

    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_record_incoming(
        generation, PEER, 48U, true));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_record_incoming(
        generation, PEER, 48U, false));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_record_i2s_underflow(
        generation, PEER));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_record_i2s_timeout(
        generation, PEER, ESP_ERR_TIMEOUT));

    TEST_ASSERT_EQUAL_size_t(0U, bt_hfp_event_command_stub_count());
}

void test_health_events_emit_only_on_severity_changes_with_monotonic_count(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    bt_hfp_event_command_stub_reset();

    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_health(
        generation, PEER, BT_AUDIO_HEALTH_DEGRADED,
        ESP_ERR_TIMEOUT, "first threshold"));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_health(
        generation, PEER, BT_AUDIO_HEALTH_DEGRADED,
        ESP_ERR_TIMEOUT, "same severity"));
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_set_health(
        generation, PEER, BT_AUDIO_HEALTH_FAULTED,
        ESP_ERR_INVALID_STATE, "second threshold"));

    TEST_ASSERT_EQUAL_size_t(2U, bt_hfp_event_command_stub_count());
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|HEALTH|DEGRADED|TIMEOUT|1|1",
        bt_hfp_event_command_stub_line(0U));
    TEST_ASSERT_EQUAL_STRING(
        "EVENT|HFP|HEALTH|FAULTED|INVALID_STATE|2|1",
        bt_hfp_event_command_stub_line(1U));
}

void test_overlong_health_text_fails_closed_without_state_change(void)
{
    uint32_t generation = begin_session(BT_DUPLEX_MODE_AUTO);
    bt_hfp_event_command_stub_reset();
    char text[BT_DUPLEX_ERROR_TEXT_LEN + 1U];
    memset(text, 'X', sizeof(text));
    text[sizeof(text) - 1U] = '\0';

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, bt_duplex_set_health(
        generation, PEER, BT_AUDIO_HEALTH_DEGRADED,
        ESP_FAIL, text));
    TEST_ASSERT_EQUAL_size_t(0U, bt_hfp_event_command_stub_count());

    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(BT_AUDIO_HEALTH_OK, snapshot.health);
    TEST_ASSERT_EQUAL(ESP_OK, snapshot.last_error);
}

void test_event_delivery_failure_is_visible_without_rolling_back_state(void)
{
    bt_hfp_event_command_stub_set_status(CMD_ERROR_NOT_INITIALIZED);
    uint32_t generation = 0U;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_session_begin(
        PEER, BT_DUPLEX_MODE_AUTO, &generation));

    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_TRUE(snapshot.peer_valid);
    TEST_ASSERT_EQUAL_UINT32(generation, snapshot.session_generation);
    TEST_ASSERT_EQUAL(BT_AUDIO_HEALTH_DEGRADED, snapshot.health);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, snapshot.last_error);
    TEST_ASSERT_EQUAL_STRING(
        "HFP event delivery failed", snapshot.last_error_text);
}

void test_stale_delivery_failure_does_not_degrade_new_session(void)
{
    uint32_t old_generation = begin_session(BT_DUPLEX_MODE_AUTO);
    bt_hfp_event_command_stub_reset();
    uint32_t new_generation = 0U;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_session_begin(
        PEER, BT_DUPLEX_MODE_AUTO, &new_generation));
    TEST_ASSERT_NOT_EQUAL(old_generation, new_generation);

    bt_duplex_record_event_delivery_failure(
        old_generation, PEER, ESP_FAIL);
    bt_duplex_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(BT_AUDIO_HEALTH_OK, snapshot.health);
    TEST_ASSERT_EQUAL(ESP_OK, snapshot.last_error);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_profile_event_is_canonical_and_bounded);
    RUN_TEST(test_global_profile_event_uses_none_and_zero_session);
    RUN_TEST(test_invalid_profile_mac_is_rejected_without_emission);
    RUN_TEST(test_audio_mode_i2s_and_health_wire_records_are_stable);
    RUN_TEST(test_i2s_event_rejects_inconsistent_error_fields);
    RUN_TEST(test_generic_failure_uses_stable_esp_fail_token);
    RUN_TEST(test_unknown_error_uses_stable_unknown_token);
    RUN_TEST(test_command_backend_error_is_propagated);
    RUN_TEST(test_session_begin_emits_profile_and_mode_once);
    RUN_TEST(test_duplicate_profile_audio_mode_and_i2s_states_are_suppressed);
    RUN_TEST(test_audio_event_uses_codec_after_authoritative_transition);
    RUN_TEST(test_error_aware_i2s_transition_preserves_exact_error);
    RUN_TEST(test_pcm_and_counter_updates_do_not_emit_per_frame_events);
    RUN_TEST(test_health_events_emit_only_on_severity_changes_with_monotonic_count);
    RUN_TEST(test_overlong_health_text_fails_closed_without_state_change);
    RUN_TEST(test_event_delivery_failure_is_visible_without_rolling_back_state);
    RUN_TEST(test_stale_delivery_failure_does_not_degrade_new_session);
    return UNITY_END();
}
