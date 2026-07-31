#include "unity.h"

void test_default_config_and_pin_validation(void);
void test_ring_allocation_failures_roll_back(void);
void test_channel_allocation_failure_rolls_back(void);
void test_mode_init_failure_rolls_back(void);
void test_task_create_failure_rolls_back(void);
void test_channel_enable_failure_stops_task_and_rolls_back(void);
void test_start_rollback_timeout_returns_timeout_and_quarantines(void);
void test_start_cleanup_failure_returns_cleanup_error_and_quarantines(void);
void test_start_stop_are_idempotent_for_same_session(void);
void test_stop_timeout_quarantines_without_freeing_live_resources(void);
void test_stale_generation_and_cvsd_duplication(void);
void test_ring_full_rejects_entire_cvsd_frame(void);
void test_writer_counts_silence_and_faults_after_bounded_failures(void);
void test_writer_skips_consumption_when_lock_is_unavailable(void);
void test_cleanup_failure_quarantines(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_config_and_pin_validation);
    RUN_TEST(test_ring_allocation_failures_roll_back);
    RUN_TEST(test_channel_allocation_failure_rolls_back);
    RUN_TEST(test_mode_init_failure_rolls_back);
    RUN_TEST(test_task_create_failure_rolls_back);
    RUN_TEST(test_channel_enable_failure_stops_task_and_rolls_back);
    RUN_TEST(test_start_rollback_timeout_returns_timeout_and_quarantines);
    RUN_TEST(test_start_cleanup_failure_returns_cleanup_error_and_quarantines);
    RUN_TEST(test_start_stop_are_idempotent_for_same_session);
    RUN_TEST(test_stop_timeout_quarantines_without_freeing_live_resources);
    RUN_TEST(test_stale_generation_and_cvsd_duplication);
    RUN_TEST(test_ring_full_rejects_entire_cvsd_frame);
    RUN_TEST(test_writer_counts_silence_and_faults_after_bounded_failures);
    RUN_TEST(test_writer_skips_consumption_when_lock_is_unavailable);
    RUN_TEST(test_cleanup_failure_quarantines);
    return UNITY_END();
}
