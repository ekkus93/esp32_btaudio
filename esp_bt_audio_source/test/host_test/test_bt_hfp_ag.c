#include "unity.h"

void test_hfp_profile_init_requires_callback_confirmation(void);
void test_hfp_callback_registration_failure_is_visible(void);
void test_hfp_init_request_failure_is_visible(void);
void test_hfp_init_callback_failure_is_visible(void);
void test_hfp_init_timeout_does_not_claim_ready(void);
void test_hfp_repeated_init_is_rejected(void);
void test_hfp_profile_deinit_requires_callback_confirmation(void);
void test_hfp_deinit_request_failure_is_visible(void);
void test_hfp_events_update_authoritative_same_peer_state(void);
void test_hfp_wrong_peer_and_response_failures_are_visible(void);
void test_hfp_invalid_and_unhandled_events_are_counted(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hfp_profile_init_requires_callback_confirmation);
    RUN_TEST(test_hfp_callback_registration_failure_is_visible);
    RUN_TEST(test_hfp_init_request_failure_is_visible);
    RUN_TEST(test_hfp_init_callback_failure_is_visible);
    RUN_TEST(test_hfp_init_timeout_does_not_claim_ready);
    RUN_TEST(test_hfp_repeated_init_is_rejected);
    RUN_TEST(test_hfp_profile_deinit_requires_callback_confirmation);
    RUN_TEST(test_hfp_deinit_request_failure_is_visible);
    RUN_TEST(test_hfp_events_update_authoritative_same_peer_state);
    RUN_TEST(test_hfp_wrong_peer_and_response_failures_are_visible);
    RUN_TEST(test_hfp_invalid_and_unhandled_events_are_counted);
    return UNITY_END();
}
