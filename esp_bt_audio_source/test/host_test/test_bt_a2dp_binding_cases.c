#include "test_bluetooth_shared.h"

#define A2DP_PEER_ONE "aa:bb:cc:dd:ee:ff"
#define A2DP_PEER_TWO "11:22:33:44:55:66"

static void set_connection_event(esp_a2d_cb_param_t *param,
                                 const uint8_t peer[6],
                                 esp_a2d_connection_state_t state)
{
    memset(param, 0, sizeof(*param));
    memcpy(param->conn_stat.remote_bda, peer,
           sizeof(param->conn_stat.remote_bda));
    param->conn_stat.state = state;
}

static void set_audio_event(esp_a2d_cb_param_t *param,
                            const uint8_t peer[6],
                            esp_a2d_audio_state_t state)
{
    memset(param, 0, sizeof(*param));
    memcpy(param->audio_stat.remote_bda, peer,
           sizeof(param->audio_stat.remote_bda));
    param->audio_stat.state = state;
}

void test_a2dp_audio_without_lifecycle_binding_is_rejected_and_counted(void)
{
    static const uint8_t peer[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    bt_events_a2dp_test_reset_binding();
    bt_manager_test_set_hfp_policy_status(
        true, A2DP_PEER_ONE, 41U, ESP_OK);
    bt_manager_test_set_hfp_policy_results(ESP_OK, ESP_OK);

    esp_a2d_cb_param_t param;
    set_audio_event(&param, peer, ESP_A2D_AUDIO_STATE_STARTED);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_AUDIO_STATE_EVT, &param);

    TEST_ASSERT_EQUAL_UINT(0U,
        bt_manager_test_get_hfp_audio_policy_calls());
    TEST_ASSERT_EQUAL_UINT(1U,
        bt_manager_test_get_stale_operation_records());

    bt_events_a2dp_binding_snapshot_t binding;
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_events_a2dp_test_get_binding(&binding));
    TEST_ASSERT_FALSE(binding.valid);
    TEST_ASSERT_EQUAL_UINT64(1U,
        binding.missing_binding_rejections);
}

void test_a2dp_binding_refreshes_after_hfp_generation_rotation(void)
{
    static const uint8_t peer[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    bt_events_a2dp_test_reset_binding();
    bt_manager_set_autostart_enabled(false);
    bt_manager_test_set_hfp_policy_results(ESP_OK, ESP_OK);
    bt_manager_test_set_hfp_policy_status(
        true, A2DP_PEER_ONE, 10U, ESP_OK);

    esp_a2d_cb_param_t param;
    set_connection_event(&param, peer, ESP_A2D_CONNECTION_STATE_CONNECTING);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_CONNECTION_STATE_EVT, &param);

    TEST_ASSERT_EQUAL_UINT(1U,
        bt_manager_test_get_hfp_profile_calls());
    TEST_ASSERT_EQUAL_UINT32(10U,
        bt_manager_test_get_last_hfp_profile_generation());

    bt_events_a2dp_binding_snapshot_t binding;
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_events_a2dp_test_get_binding(&binding));
    TEST_ASSERT_TRUE(binding.valid);
    TEST_ASSERT_EQUAL_STRING(A2DP_PEER_ONE, binding.peer_mac);
    TEST_ASSERT_EQUAL_UINT32(10U, binding.last_duplex_generation);

    /* HFP audio start rotates the transient duplex generation without
     * replacing the A2DP connection lifecycle. The next bound A2DP event must
     * refresh to the new generation only after validating the binding. */
    bt_manager_test_set_hfp_policy_status(
        true, A2DP_PEER_ONE, 11U, ESP_OK);
    set_audio_event(&param, peer, ESP_A2D_AUDIO_STATE_STARTED);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_AUDIO_STATE_EVT, &param);

    TEST_ASSERT_EQUAL_UINT(1U,
        bt_manager_test_get_hfp_audio_policy_calls());
    TEST_ASSERT_EQUAL_UINT32(11U,
        bt_manager_test_get_last_hfp_audio_generation());
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_events_a2dp_test_get_binding(&binding));
    TEST_ASSERT_TRUE(binding.valid);
    TEST_ASSERT_EQUAL_UINT32(11U, binding.last_duplex_generation);
    TEST_ASSERT_EQUAL_UINT64(0U, binding.generation_sync_failures);
}

void test_a2dp_wrong_peer_event_is_rejected_and_counted(void)
{
    static const uint8_t peer_one[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    static const uint8_t peer_two[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    bt_events_a2dp_test_reset_binding();
    bt_manager_set_autostart_enabled(false);
    bt_manager_test_set_hfp_policy_results(ESP_OK, ESP_OK);
    bt_manager_test_set_hfp_policy_status(
        true, A2DP_PEER_ONE, 20U, ESP_OK);

    esp_a2d_cb_param_t param;
    set_connection_event(&param, peer_one,
                         ESP_A2D_CONNECTION_STATE_CONNECTING);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_CONNECTION_STATE_EVT, &param);
    TEST_ASSERT_EQUAL_UINT(1U,
        bt_manager_test_get_hfp_profile_calls());

    set_audio_event(&param, peer_two, ESP_A2D_AUDIO_STATE_STARTED);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_AUDIO_STATE_EVT, &param);

    TEST_ASSERT_EQUAL_UINT(0U,
        bt_manager_test_get_hfp_audio_policy_calls());
    TEST_ASSERT_EQUAL_UINT(1U,
        bt_manager_test_get_stale_operation_records());

    bt_events_a2dp_binding_snapshot_t binding;
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_events_a2dp_test_get_binding(&binding));
    TEST_ASSERT_TRUE(binding.valid);
    TEST_ASSERT_EQUAL_STRING(A2DP_PEER_ONE, binding.peer_mac);
    TEST_ASSERT_EQUAL_UINT64(1U, binding.wrong_peer_rejections);
    (void)A2DP_PEER_TWO;
}
