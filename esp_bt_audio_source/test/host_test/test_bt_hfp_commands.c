#include "unity.h"

void test_hfp_parser_accepts_all_valid_forms(void);
void test_hfp_status_uses_one_consistent_snapshot_and_sanitizes_text(void);
void test_hfp_connect_reports_acceptance_not_completion(void);
void test_hfp_connect_already_connected_is_explicit(void);
void test_hfp_connect_exact_backend_error_is_reported(void);
void test_hfp_disconnect_acceptance_and_idempotence_are_distinct(void);
void test_hfp_audio_start_stop_only_claim_confirmed_success(void);
void test_hfp_mode_parsing_is_exact(void);
void test_hfp_codec_reports_authoritative_generation(void);
void test_hfp_stats_max_values_are_split_without_truncation(void);
void test_hfp_resetstats_reports_exact_success_or_failure(void);
void test_hfp_invalid_forms_are_explicit(void);
void test_response_overflow_fails_closed_without_buffer_overread(void);
void test_hfp_connect_invalid_mac_preserves_exact_backend_error(void);
void test_hfp_connect_pre_status_failure_does_not_block_accepted_request(void);
void test_hfp_audio_success_is_not_retracted_by_status_failure(void);
void test_hfp_mode_success_does_not_require_followup_snapshot(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hfp_parser_accepts_all_valid_forms);
    RUN_TEST(test_hfp_status_uses_one_consistent_snapshot_and_sanitizes_text);
    RUN_TEST(test_hfp_connect_reports_acceptance_not_completion);
    RUN_TEST(test_hfp_connect_already_connected_is_explicit);
    RUN_TEST(test_hfp_connect_exact_backend_error_is_reported);
    RUN_TEST(test_hfp_disconnect_acceptance_and_idempotence_are_distinct);
    RUN_TEST(test_hfp_audio_start_stop_only_claim_confirmed_success);
    RUN_TEST(test_hfp_mode_parsing_is_exact);
    RUN_TEST(test_hfp_codec_reports_authoritative_generation);
    RUN_TEST(test_hfp_stats_max_values_are_split_without_truncation);
    RUN_TEST(test_hfp_resetstats_reports_exact_success_or_failure);
    RUN_TEST(test_hfp_invalid_forms_are_explicit);
    RUN_TEST(test_response_overflow_fails_closed_without_buffer_overread);
    RUN_TEST(test_hfp_connect_invalid_mac_preserves_exact_backend_error);
    RUN_TEST(test_hfp_connect_pre_status_failure_does_not_block_accepted_request);
    RUN_TEST(test_hfp_audio_success_is_not_retracted_by_status_failure);
    RUN_TEST(test_hfp_mode_success_does_not_require_followup_snapshot);
    return UNITY_END();
}
