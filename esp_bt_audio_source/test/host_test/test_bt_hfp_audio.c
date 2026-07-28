#include "unity.h"

void test_callback_registration_success_and_failure_are_visible(void);
void test_activation_requires_same_peer_slc_cvsd_and_running_i2s(void);
void test_wrong_peer_event_does_not_disable_active_session(void);
void test_null_zero_odd_oversize_and_capacity_mismatch_are_rejected(void);
void test_inactive_and_deactivated_callbacks_are_rejected(void);
void test_wrong_sync_handle_is_rejected(void);
void test_bad_frame_is_rejected(void);
void test_unexpected_msbc_is_rejected_visibly(void);
void test_valid_unaligned_cvsd_frame_is_copied_whole(void);
void test_ring_rejection_counts_exact_frame_and_bytes(void);
void test_stale_generation_is_rejected_by_generation_bound_i2s(void);
void test_callback_timing_tracks_last_max_and_budget(void);
void test_cleanup_refuses_while_full_callback_lifetime_is_active(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_callback_registration_success_and_failure_are_visible);
    RUN_TEST(test_activation_requires_same_peer_slc_cvsd_and_running_i2s);
    RUN_TEST(test_wrong_peer_event_does_not_disable_active_session);
    RUN_TEST(test_null_zero_odd_oversize_and_capacity_mismatch_are_rejected);
    RUN_TEST(test_inactive_and_deactivated_callbacks_are_rejected);
    RUN_TEST(test_wrong_sync_handle_is_rejected);
    RUN_TEST(test_bad_frame_is_rejected);
    RUN_TEST(test_unexpected_msbc_is_rejected_visibly);
    RUN_TEST(test_valid_unaligned_cvsd_frame_is_copied_whole);
    RUN_TEST(test_ring_rejection_counts_exact_frame_and_bytes);
    RUN_TEST(test_stale_generation_is_rejected_by_generation_bound_i2s);
    RUN_TEST(test_callback_timing_tracks_last_max_and_budget);
    RUN_TEST(test_cleanup_refuses_while_full_callback_lifetime_is_active);
    return UNITY_END();
}
