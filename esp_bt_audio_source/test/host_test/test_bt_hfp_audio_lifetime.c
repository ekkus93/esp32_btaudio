#include "unity.h"

#include "bt_hfp_audio.h"

void test_callback_lifetime_metrics_survive_profile_teardown(void)
{
    bt_hfp_audio_snapshot_t snapshot;
    const uint32_t over_budget = BT_HFP_AUDIO_CALLBACK_BUDGET_US + 77U;

    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_register_callback());
    bt_hfp_audio_test_record_callback_duration(over_budget);
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT32(over_budget, snapshot.callback_last_us);
    TEST_ASSERT_EQUAL_UINT32(over_budget, snapshot.callback_max_us);
    TEST_ASSERT_EQUAL_UINT64(1U, snapshot.callback_over_budget);

    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_hfp_audio_cleanup_after_stack_shutdown());

    /* A new HFP profile lifecycle may reset current/session counters, but it
     * must not erase values exposed as process-lifetime diagnostics. */
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_register_callback());
    bt_hfp_audio_test_record_callback_duration(25U);
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT32(25U, snapshot.callback_last_us);
    TEST_ASSERT_EQUAL_UINT32(over_budget, snapshot.callback_max_us);
    TEST_ASSERT_EQUAL_UINT64(1U, snapshot.callback_over_budget);
}
