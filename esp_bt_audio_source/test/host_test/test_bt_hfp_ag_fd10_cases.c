#include "unity.h"

#include "bt_duplex_state.h"
#include "bt_hfp_ag.h"

void mock_bt_hfp_audio_set_control_init_result(esp_err_t result);
unsigned mock_bt_hfp_audio_register_calls(void);
unsigned mock_bt_hfp_audio_control_init_calls(void);

void test_hfp_audio_control_initializes_after_callback_registration(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_profile_init(10U));
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_audio_register_calls());
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_audio_control_init_calls());
}

void test_hfp_audio_control_initialization_failure_is_visible(void)
{
    mock_bt_hfp_audio_set_control_init_result(ESP_ERR_NO_MEM);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, bt_hfp_ag_profile_init(10U));

    bt_hfp_ag_snapshot_t hfp;
    bt_duplex_snapshot_t duplex;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_get_snapshot(&hfp));
    TEST_ASSERT_EQUAL(BT_HFP_AG_LIFECYCLE_FAULTED, hfp.lifecycle);
    TEST_ASSERT_FALSE(hfp.profile_ready);
    TEST_ASSERT_TRUE(hfp.profile_init_request_accepted);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, hfp.last_error);
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_audio_register_calls());
    TEST_ASSERT_EQUAL_UINT(1U, mock_bt_hfp_audio_control_init_calls());
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_FAULTED,
                      duplex.hfp_profile_state);
}
