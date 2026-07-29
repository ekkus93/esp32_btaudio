#include <string.h>

#include "unity.h"
#include "bt_events_a2dp.h"
#include "bt_manager.h"
#include "bt_manager_internal.h"
#include "mock_a2dp.h"
#include "mock_avrc.h"
#include "mock_gap.h"

extern void nvs_storage_mock_reset(void);
extern void bt_manager_test_reset_forces(void);
extern void bt_manager_test_reset_btstate_mock(void);
extern void bt_manager_test_set_hfp_policy_status(bool peer_valid,
                                                   const char *peer,
                                                   uint32_t generation,
                                                   esp_err_t result);
extern void bt_manager_test_set_hfp_policy_results(esp_err_t profile_result,
                                                    esp_err_t audio_result);
extern void bt_manager_test_set_hfp_profile_created_generation(
    uint32_t generation);
extern unsigned bt_manager_test_get_hfp_audio_policy_calls(void);
extern unsigned bt_manager_test_get_stale_operation_records(void);
extern int bt_manager_test_get_last_audio_state(void);

static const uint8_t PEER_A_BDA[6] = {
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
};
static const uint8_t PEER_B_BDA[6] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66
};
static const char *PEER_A = "aa:bb:cc:dd:ee:ff";
static const char *PEER_B = "11:22:33:44:55:66";

static void send_connection(esp_a2d_connection_state_t state,
                            const uint8_t bda[6],
                            esp_a2d_conn_hdl_t handle)
{
    esp_a2d_cb_param_t param = {0};
    param.conn_stat.state = state;
    memcpy(param.conn_stat.remote_bda, bda, 6U);
    param.conn_stat.conn_hdl = handle;
    bt_events_handle_a2dp_connection(&param);
}

static void send_audio(esp_a2d_audio_state_t state,
                       const uint8_t bda[6],
                       esp_a2d_conn_hdl_t handle)
{
    esp_a2d_cb_param_t param = {0};
    param.audio_stat.state = state;
    memcpy(param.audio_stat.remote_bda, bda, 6U);
    param.audio_stat.conn_hdl = handle;
    bt_events_handle_a2dp_audio(&param);
}

static bt_events_a2dp_binding_snapshot_t binding_snapshot(void)
{
    bt_events_a2dp_binding_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&snapshot));
    return snapshot;
}

static void establish_bound_session(const uint8_t bda[6], const char *peer,
                                    esp_a2d_conn_hdl_t handle,
                                    uint32_t generation)
{
    bt_manager_test_set_hfp_policy_status(false, NULL, 0U, ESP_OK);
    bt_manager_test_set_hfp_policy_results(ESP_OK, ESP_OK);
    bt_manager_test_set_hfp_profile_created_generation(generation);
    send_connection(ESP_A2D_CONNECTION_STATE_CONNECTED, bda, handle);

    const bt_events_a2dp_binding_snapshot_t snapshot = binding_snapshot();
    TEST_ASSERT_TRUE(snapshot.valid);
    TEST_ASSERT_EQUAL_STRING(peer, snapshot.peer_mac);
    TEST_ASSERT_EQUAL_UINT(handle, snapshot.conn_handle);
    TEST_ASSERT_EQUAL_UINT32(generation,
                             snapshot.last_duplex_generation);
}

static void assert_active_identity_unchanged(
    const bt_events_a2dp_binding_snapshot_t *before,
    const bt_events_a2dp_binding_snapshot_t *after)
{
    TEST_ASSERT_TRUE(after->valid);
    TEST_ASSERT_EQUAL_STRING(before->peer_mac, after->peer_mac);
    TEST_ASSERT_EQUAL_UINT(before->conn_handle, after->conn_handle);
    TEST_ASSERT_EQUAL_UINT32(before->lifecycle_serial,
                             after->lifecycle_serial);
    TEST_ASSERT_EQUAL_UINT32(before->last_duplex_generation,
                             after->last_duplex_generation);
    TEST_ASSERT_EQUAL_UINT64(before->missing_binding_rejections,
                             after->missing_binding_rejections);
    TEST_ASSERT_EQUAL_UINT64(before->generation_sync_failures,
                             after->generation_sync_failures);
    TEST_ASSERT_EQUAL_UINT64(before->late_terminal_events_ignored,
                             after->late_terminal_events_ignored);
}

void setUp(void)
{
    mock_a2dp_reset();
    mock_avrc_reset();
    mock_gap_reset();
    nvs_storage_mock_reset();
    bt_manager_test_reset_forces();
    bt_manager_test_reset_btstate_mock();
    memset(&bt_ctx, 0, sizeof(bt_ctx));
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_test_init_mutex());
    TEST_ASSERT_EQUAL(ESP_OK, bt_events_a2dp_test_reset_binding());
}

void tearDown(void)
{
    bt_manager_test_deinit_mutex();
}

void test_delayed_remote_suspend_from_old_peer_cannot_change_new_session(void)
{
    establish_bound_session(PEER_A_BDA, PEER_A, 7, 41U);
    send_audio(ESP_A2D_AUDIO_STATE_STARTED, PEER_A_BDA, 7);
    send_connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED, PEER_A_BDA, 7);

    establish_bound_session(PEER_B_BDA, PEER_B, 9, 42U);
    send_audio(ESP_A2D_AUDIO_STATE_STARTED, PEER_B_BDA, 9);
    TEST_ASSERT_TRUE(bt_ctx.audio_playing);

    const bt_events_a2dp_binding_snapshot_t before = binding_snapshot();
    const unsigned policy_calls =
        bt_manager_test_get_hfp_audio_policy_calls();
    const unsigned stale_records =
        bt_manager_test_get_stale_operation_records();
    const int last_audio_state = bt_manager_test_get_last_audio_state();

    send_audio(ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND, PEER_A_BDA, 7);

    const bt_events_a2dp_binding_snapshot_t after = binding_snapshot();
    TEST_ASSERT_TRUE(bt_ctx.audio_playing);
    TEST_ASSERT_EQUAL_UINT(policy_calls,
        bt_manager_test_get_hfp_audio_policy_calls());
    TEST_ASSERT_EQUAL_UINT(stale_records + 1U,
        bt_manager_test_get_stale_operation_records());
    TEST_ASSERT_EQUAL_INT(last_audio_state,
                          bt_manager_test_get_last_audio_state());
    assert_active_identity_unchanged(&before, &after);
    TEST_ASSERT_EQUAL_UINT64(before.wrong_peer_rejections + 1U,
                             after.wrong_peer_rejections);
    TEST_ASSERT_EQUAL_UINT64(before.stale_handle_rejections,
                             after.stale_handle_rejections);
}

void test_delayed_stop_with_old_handle_cannot_change_reconnected_same_peer(void)
{
    establish_bound_session(PEER_A_BDA, PEER_A, 7, 41U);
    const bt_events_a2dp_binding_snapshot_t session_a = binding_snapshot();
    send_audio(ESP_A2D_AUDIO_STATE_STARTED, PEER_A_BDA, 7);
    send_connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED, PEER_A_BDA, 7);

    establish_bound_session(PEER_A_BDA, PEER_A, 9, 42U);
    send_audio(ESP_A2D_AUDIO_STATE_STARTED, PEER_A_BDA, 9);
    TEST_ASSERT_TRUE(bt_ctx.audio_playing);

    const bt_events_a2dp_binding_snapshot_t before = binding_snapshot();
    TEST_ASSERT_EQUAL_UINT32(session_a.lifecycle_serial + 1U,
                             before.lifecycle_serial);
    const unsigned policy_calls =
        bt_manager_test_get_hfp_audio_policy_calls();
    const unsigned stale_records =
        bt_manager_test_get_stale_operation_records();
    const int last_audio_state = bt_manager_test_get_last_audio_state();

    send_audio(ESP_A2D_AUDIO_STATE_STOPPED, PEER_A_BDA, 7);

    const bt_events_a2dp_binding_snapshot_t after = binding_snapshot();
    TEST_ASSERT_TRUE(bt_ctx.audio_playing);
    TEST_ASSERT_EQUAL_UINT(policy_calls,
        bt_manager_test_get_hfp_audio_policy_calls());
    TEST_ASSERT_EQUAL_UINT(stale_records + 1U,
        bt_manager_test_get_stale_operation_records());
    TEST_ASSERT_EQUAL_INT(last_audio_state,
                          bt_manager_test_get_last_audio_state());
    assert_active_identity_unchanged(&before, &after);
    TEST_ASSERT_EQUAL_UINT64(before.stale_handle_rejections + 1U,
                             after.stale_handle_rejections);
    TEST_ASSERT_EQUAL_UINT64(before.wrong_peer_rejections,
                             after.wrong_peer_rejections);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(
        test_delayed_remote_suspend_from_old_peer_cannot_change_new_session);
    RUN_TEST(
        test_delayed_stop_with_old_handle_cannot_change_reconnected_same_peer);
    return UNITY_END();
}
