#include "unity.h"

/* Compile the focused RF-03 fault-injection cases into the established control
 * runner without duplicating its large explicit CMake source list. */
#include "test_bt_hfp_audio_control_health_cases.c"

void test_audio_start_requires_slc_and_registered_callback(void);
void test_audio_start_starts_i2s_before_sco_and_waits_for_confirmation(void);
void test_duplicate_audio_start_is_rejected_without_second_request(void);
void test_i2s_start_failure_prevents_sco_request(void);
void test_immediate_sco_connect_failure_rolls_back_i2s(void);
void test_confirmed_connected_route_failure_rolls_back_and_disconnects(void);
void test_unsolicited_connected_event_is_rejected_without_fast_route(void);
void test_audio_stop_handles_connecting_and_connected_states(void);
void test_audio_disconnect_timeout_faults_but_stops_i2s_and_preserves_peer(void);
void test_i2s_stop_timeout_is_quarantined_and_takes_precedence(void);
void test_late_old_connected_event_after_timeout_is_ignored(void);
void test_dispatch_failure_rolls_back_without_lower_request(void);
void test_msbc_confirmation_is_visibly_rejected(void);
void test_wrong_peer_event_does_not_complete_operation(void);
void test_profile_stopping_closes_fast_gate_before_control_init(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_audio_start_requires_slc_and_registered_callback);
    RUN_TEST(test_audio_start_starts_i2s_before_sco_and_waits_for_confirmation);
    RUN_TEST(test_duplicate_audio_start_is_rejected_without_second_request);
    RUN_TEST(test_i2s_start_failure_prevents_sco_request);
    RUN_TEST(test_immediate_sco_connect_failure_rolls_back_i2s);
    RUN_TEST(test_confirmed_connected_route_failure_rolls_back_and_disconnects);
    RUN_TEST(test_unsolicited_connected_event_is_rejected_without_fast_route);
    RUN_TEST(test_audio_stop_handles_connecting_and_connected_states);
    RUN_TEST(test_audio_disconnect_timeout_faults_but_stops_i2s_and_preserves_peer);
    RUN_TEST(test_i2s_stop_timeout_is_quarantined_and_takes_precedence);
    RUN_TEST(test_late_old_connected_event_after_timeout_is_ignored);
    RUN_TEST(test_dispatch_failure_rolls_back_without_lower_request);
    RUN_TEST(test_msbc_confirmation_is_visibly_rejected);
    RUN_TEST(test_wrong_peer_event_does_not_complete_operation);
    RUN_TEST(test_profile_stopping_closes_fast_gate_before_control_init);
    RUN_TEST(test_health_report_failure_after_i2s_start_failure_is_visible);
    RUN_TEST(test_health_report_failure_after_i2s_stop_failure_is_visible);
    RUN_TEST(test_health_report_failure_after_connect_event_timeout_is_visible);
    RUN_TEST(test_health_report_failure_after_disconnect_event_timeout_is_visible);
    RUN_TEST(test_health_report_failure_during_rollback_is_visible);
    RUN_TEST(test_health_report_failure_during_remote_cleanup_is_visible);
    return UNITY_END();
}
