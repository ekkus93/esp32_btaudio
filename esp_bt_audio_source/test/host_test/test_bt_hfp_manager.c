#include "unity.h"

void test_manager_status_returns_configured_mode_and_one_duplex_snapshot(void);
void test_manager_mode_updates_atomically_and_rejects_live_audio(void);
void test_manager_connect_carries_configured_mode_into_new_session(void);
void test_manager_connect_rejects_non_active_peer_before_lower_request(void);
void test_manager_connect_rejects_malformed_mac_exactly(void);
void test_manager_wrappers_preserve_exact_lower_errors(void);
void test_manager_stats_reset_uses_non_destructive_baseline(void);
void test_manager_resetstats_rejects_live_slc_audio_callback_and_i2s(void);
void test_manager_resetstats_rejects_authoritative_streaming_state(void);
void test_manager_stats_detects_regressed_lifetime_source(void);
void test_manager_stats_detects_regressed_lifetime_maximum(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_manager_status_returns_configured_mode_and_one_duplex_snapshot);
    RUN_TEST(test_manager_mode_updates_atomically_and_rejects_live_audio);
    RUN_TEST(test_manager_connect_carries_configured_mode_into_new_session);
    RUN_TEST(test_manager_connect_rejects_non_active_peer_before_lower_request);
    RUN_TEST(test_manager_connect_rejects_malformed_mac_exactly);
    RUN_TEST(test_manager_wrappers_preserve_exact_lower_errors);
    RUN_TEST(test_manager_stats_reset_uses_non_destructive_baseline);
    RUN_TEST(test_manager_resetstats_rejects_live_slc_audio_callback_and_i2s);
    RUN_TEST(test_manager_resetstats_rejects_authoritative_streaming_state);
    RUN_TEST(test_manager_stats_detects_regressed_lifetime_source);
    RUN_TEST(test_manager_stats_detects_regressed_lifetime_maximum);
    return UNITY_END();
}
