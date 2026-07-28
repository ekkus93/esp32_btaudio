#include "unity.h"

void test_initial_state_is_deterministic(void);
void test_same_peer_is_case_insensitive_and_different_peer_is_rejected(void);
void test_hfp_profile_legal_and_illegal_transitions_are_explicit(void);
void test_same_peer_restart_is_rejected_while_transient_resources_are_live(void);
void test_stale_generation_is_ignored_and_counted(void);
void test_codec_is_derived_from_confirmed_audio_state(void);
void test_fault_cannot_be_quietly_downgraded_and_recovery_rotates_generation(void);
void test_recovery_rejects_healthy_or_live_resources(void);
void test_snapshot_never_exposes_torn_64_bit_counter_group(void);
void test_negative_enum_values_are_rejected(void);
void test_a2dp_transitions_are_checked(void);
void test_audio_generation_rotation_preserves_session_state_and_telemetry(void);
void test_audio_generation_rotation_rejects_invalid_transient_states(void);
void test_strings_are_stable_and_invalid_values_are_visible(void);
void test_all_state_strings_are_exhaustive(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_deterministic);
    RUN_TEST(test_same_peer_is_case_insensitive_and_different_peer_is_rejected);
    RUN_TEST(test_hfp_profile_legal_and_illegal_transitions_are_explicit);
    RUN_TEST(test_same_peer_restart_is_rejected_while_transient_resources_are_live);
    RUN_TEST(test_stale_generation_is_ignored_and_counted);
    RUN_TEST(test_codec_is_derived_from_confirmed_audio_state);
    RUN_TEST(test_fault_cannot_be_quietly_downgraded_and_recovery_rotates_generation);
    RUN_TEST(test_recovery_rejects_healthy_or_live_resources);
    RUN_TEST(test_snapshot_never_exposes_torn_64_bit_counter_group);
    RUN_TEST(test_negative_enum_values_are_rejected);
    RUN_TEST(test_a2dp_transitions_are_checked);
    RUN_TEST(test_audio_generation_rotation_preserves_session_state_and_telemetry);
    RUN_TEST(test_audio_generation_rotation_rejects_invalid_transient_states);
    RUN_TEST(test_strings_are_stable_and_invalid_values_are_visible);
    RUN_TEST(test_all_state_strings_are_exhaustive);
    return UNITY_END();
}
