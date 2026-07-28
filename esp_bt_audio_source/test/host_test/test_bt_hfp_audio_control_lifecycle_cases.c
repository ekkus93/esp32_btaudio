#include "unity.h"

#include "bt_hfp_audio.h"

unsigned mock_hfp_audio_control_profile_stopping_calls(void);

void test_profile_stopping_closes_fast_gate_before_control_init(void)
{
    TEST_ASSERT_EQUAL_UINT(0U,
                           mock_hfp_audio_control_profile_stopping_calls());
    bt_hfp_audio_control_profile_stopping();
    TEST_ASSERT_EQUAL_UINT(1U,
                           mock_hfp_audio_control_profile_stopping_calls());

    bt_hfp_audio_control_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_hfp_audio_control_get_snapshot(&snapshot));
}
