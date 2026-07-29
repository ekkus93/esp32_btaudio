#include "test_bluetooth_shared.h"

#define A2DP_PEER_ONE "aa:bb:cc:dd:ee:ff"
#define A2DP_HANDLE_ONE ((esp_a2d_conn_hdl_t)0x0101U)
#define A2DP_HANDLE_TWO ((esp_a2d_conn_hdl_t)0x0202U)

static void set_connection_event(esp_a2d_cb_param_t *param,
                                 const uint8_t peer[6],
                                 esp_a2d_conn_hdl_t conn_handle,
                                 esp_a2d_connection_state_t state)
{
    memset(param, 0, sizeof(*param));
    memcpy(param->conn_stat.remote_bda, peer,
           sizeof(param->conn_stat.remote_bda));
    param->conn_stat.conn_hdl = conn_handle;
    param->conn_stat.state = state;
}

static void set_audio_event(esp_a2d_cb_param_t *param,
                            const uint8_t peer[6],
                            esp_a2d_conn_hdl_t conn_handle,
                            esp_a2d_audio_state_t state)
{
    memset(param, 0, sizeof(*param));
    memcpy(param->audio_stat.remote_bda, peer,
           sizeof(param->audio_stat.remote_bda));
    param->audio_stat.conn_hdl = conn_handle;
    param->audio_stat.state = state;
}

void test_a2dp_first_profile_event_creates_and_binds_session(void)
{
    static const uint8_t peer[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    bt_events_a2dp_test_reset_binding();
    bt_manager_set_autostart_enabled(false);
    bt_manager_test_set_hfp_policy_status(false, NULL, 0U, ESP_OK);
    bt_manager_test_set_hfp_policy_results(ESP_OK, ESP_OK);
    bt_manager_test_set_hfp_profile_created_generation(7U);

    esp_a2d_cb_param_t param;
    set_connection_event(&param, peer, A2DP_HANDLE_ONE,
                         ESP_A2D_CONNECTION_STATE_CONNECTING);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_CONNECTION_STATE_EVT, &param);

    TEST_ASSERT_EQUAL_UINT(1U,
        bt_manager_test_get_hfp_profile_calls());
    TEST_ASSERT_EQUAL_UINT32(0U,
        bt_manager_test_get_last_hfp_profile_generation());

    bt_events_a2dp_binding_snapshot_t binding;
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_events_a2dp_test_get_binding(&binding));
    TEST_ASSERT_TRUE(binding.valid);
    TEST_ASSERT_EQUAL_STRING(A2DP_PEER_ONE, binding.peer_mac);
    TEST_ASSERT_EQUAL_UINT16(A2DP_HANDLE_ONE, binding.conn_handle);
    TEST_ASSERT_EQUAL_UINT32(7U, binding.last_duplex_generation);
    TEST_ASSERT_NOT_EQUAL(0U, binding.lifecycle_serial);
}

void test_a2dp_reconnect_rotates_lifecycle_serial(void)
{
    static const uint8_t peer[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    bt_events_a2dp_test_reset_binding();
    bt_manager_set_autostart_enabled(false);
    bt_manager_test_set_hfp_policy_results(ESP_OK, ESP_OK);
    bt_manager_test_set_hfp_policy_status(
        true, A2DP_PEER_ONE, 30U, ESP_OK);

    esp_a2d_cb_param_t param;
    set_connection_event(&param, peer, A2DP_HANDLE_ONE,
                         ESP_A2D_CONNECTION_STATE_CONNECTING);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_CONNECTION_STATE_EVT, &param);

    bt_events_a2dp_binding_snapshot_t first;
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_events_a2dp_test_get_binding(&first));
    TEST_ASSERT_TRUE(first.valid);
    TEST_ASSERT_EQUAL_UINT16(A2DP_HANDLE_ONE, first.conn_handle);
    TEST_ASSERT_EQUAL_UINT32(30U, first.last_duplex_generation);

    set_connection_event(&param, peer, A2DP_HANDLE_ONE,
                         ESP_A2D_CONNECTION_STATE_DISCONNECTED);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_CONNECTION_STATE_EVT, &param);

    bt_events_a2dp_binding_snapshot_t cleared;
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_events_a2dp_test_get_binding(&cleared));
    TEST_ASSERT_FALSE(cleared.valid);
    TEST_ASSERT_EQUAL_UINT32(first.lifecycle_serial,
                             cleared.lifecycle_serial);

    bt_manager_test_set_hfp_policy_status(
        true, A2DP_PEER_ONE, 31U, ESP_OK);
    set_connection_event(&param, peer, A2DP_HANDLE_TWO,
                         ESP_A2D_CONNECTION_STATE_CONNECTING);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_CONNECTION_STATE_EVT, &param);

    bt_events_a2dp_binding_snapshot_t second;
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_events_a2dp_test_get_binding(&second));
    TEST_ASSERT_TRUE(second.valid);
    TEST_ASSERT_EQUAL_UINT16(A2DP_HANDLE_TWO, second.conn_handle);
    TEST_ASSERT_NOT_EQUAL(first.lifecycle_serial,
                          second.lifecycle_serial);
    TEST_ASSERT_EQUAL_UINT32(31U, second.last_duplex_generation);
}

void test_a2dp_audio_without_lifecycle_binding_is_rejected_and_counted(void)
{
    static const uint8_t peer[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    bt_events_a2dp_test_reset_binding();
    bt_manager_test_set_hfp_policy_status(
        true, A2DP_PEER_ONE, 41U, ESP_OK);
    bt_manager_test_set_hfp_policy_results(ESP_OK, ESP_OK);

    esp_a2d_cb_param_t param;
    set_audio_event(&param, peer, A2DP_HANDLE_ONE,
                    ESP_A2D_AUDIO_STATE_STARTED);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_AUDIO_STATE_EVT, &param);

    TEST_ASSERT_FALSE(bt_manager_test_is_audio_playing());
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
    set_connection_event(&param, peer, A2DP_HANDLE_ONE,
                         ESP_A2D_CONNECTION_STATE_CONNECTING);
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
    TEST_ASSERT_EQUAL_UINT16(A2DP_HANDLE_ONE, binding.conn_handle);
    TEST_ASSERT_EQUAL_UINT32(10U, binding.last_duplex_generation);

    bt_manager_test_set_hfp_policy_status(
        true, A2DP_PEER_ONE, 11U, ESP_OK);
    set_audio_event(&param, peer, A2DP_HANDLE_ONE,
                    ESP_A2D_AUDIO_STATE_STARTED);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_AUDIO_STATE_EVT, &param);

    TEST_ASSERT_TRUE(bt_manager_test_is_audio_playing());
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
    set_connection_event(&param, peer_one, A2DP_HANDLE_ONE,
                         ESP_A2D_CONNECTION_STATE_CONNECTING);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_CONNECTION_STATE_EVT, &param);
    TEST_ASSERT_EQUAL_UINT(1U,
        bt_manager_test_get_hfp_profile_calls());

    set_audio_event(&param, peer_two, A2DP_HANDLE_ONE,
                    ESP_A2D_AUDIO_STATE_STARTED);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_AUDIO_STATE_EVT, &param);

    TEST_ASSERT_FALSE(bt_manager_test_is_audio_playing());
    TEST_ASSERT_EQUAL_UINT(0U,
        bt_manager_test_get_hfp_audio_policy_calls());
    TEST_ASSERT_EQUAL_UINT(1U,
        bt_manager_test_get_stale_operation_records());

    bt_events_a2dp_binding_snapshot_t binding;
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_events_a2dp_test_get_binding(&binding));
    TEST_ASSERT_TRUE(binding.valid);
    TEST_ASSERT_EQUAL_STRING(A2DP_PEER_ONE, binding.peer_mac);
    TEST_ASSERT_EQUAL_UINT16(A2DP_HANDLE_ONE, binding.conn_handle);
    TEST_ASSERT_EQUAL_UINT64(1U, binding.wrong_peer_rejections);
}

void test_a2dp_stale_same_peer_handle_cannot_mutate_new_connection(void)
{
    static const uint8_t peer[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    bt_events_a2dp_test_reset_binding();
    bt_manager_set_autostart_enabled(false);
    bt_manager_test_set_hfp_policy_results(ESP_OK, ESP_OK);
    bt_manager_test_set_hfp_policy_status(
        true, A2DP_PEER_ONE, 50U, ESP_OK);

    esp_a2d_cb_param_t param;
    set_connection_event(&param, peer, A2DP_HANDLE_ONE,
                         ESP_A2D_CONNECTION_STATE_CONNECTED);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_CONNECTION_STATE_EVT, &param);
    set_connection_event(&param, peer, A2DP_HANDLE_ONE,
                         ESP_A2D_CONNECTION_STATE_DISCONNECTED);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_CONNECTION_STATE_EVT, &param);

    bt_manager_test_set_hfp_policy_status(
        true, A2DP_PEER_ONE, 51U, ESP_OK);
    set_connection_event(&param, peer, A2DP_HANDLE_TWO,
                         ESP_A2D_CONNECTION_STATE_CONNECTED);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_CONNECTION_STATE_EVT, &param);

    bt_manager_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_get_status(&status));
    TEST_ASSERT_TRUE(status.connected);
    TEST_ASSERT_FALSE(status.audio_playing);

    const unsigned audio_calls_before =
        bt_manager_test_get_hfp_audio_policy_calls();
    set_audio_event(&param, peer, A2DP_HANDLE_ONE,
                    ESP_A2D_AUDIO_STATE_STARTED);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_AUDIO_STATE_EVT, &param);
    TEST_ASSERT_EQUAL_UINT(audio_calls_before,
        bt_manager_test_get_hfp_audio_policy_calls());
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_get_status(&status));
    TEST_ASSERT_TRUE(status.connected);
    TEST_ASSERT_FALSE(status.audio_playing);

    bt_disconnected_callback_called = false;
    set_connection_event(&param, peer, A2DP_HANDLE_ONE,
                         ESP_A2D_CONNECTION_STATE_DISCONNECTED);
    bt_manager_test_invoke_a2dp_event(ESP_A2D_CONNECTION_STATE_EVT, &param);
    TEST_ASSERT_FALSE(bt_disconnected_callback_called);
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_get_status(&status));
    TEST_ASSERT_TRUE(status.connected);

    bt_events_a2dp_binding_snapshot_t binding;
    TEST_ASSERT_EQUAL(ESP_OK,
        bt_events_a2dp_test_get_binding(&binding));
    TEST_ASSERT_TRUE(binding.valid);
    TEST_ASSERT_EQUAL_UINT16(A2DP_HANDLE_TWO, binding.conn_handle);
    TEST_ASSERT_EQUAL_UINT32(51U, binding.last_duplex_generation);
    TEST_ASSERT_EQUAL_UINT64(2U, binding.stale_handle_rejections);
    TEST_ASSERT_EQUAL_UINT(2U,
        bt_manager_test_get_stale_operation_records());
}
