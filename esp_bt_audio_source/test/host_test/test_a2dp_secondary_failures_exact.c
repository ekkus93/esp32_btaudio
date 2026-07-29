#include <string.h>

#include "unity.h"
#include "bt_events_a2dp.h"
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
extern unsigned bt_manager_test_get_hfp_status_calls(void);
extern unsigned bt_manager_test_get_hfp_audio_policy_calls(void);
extern int bt_manager_test_get_last_audio_state(void);

static const uint8_t PEER_BDA[6] = {
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
};
static const char *PEER = "aa:bb:cc:dd:ee:ff";
static const esp_a2d_conn_hdl_t HANDLE = 7;

static void send_connection(esp_a2d_connection_state_t state)
{
    esp_a2d_cb_param_t param = {0};
    param.conn_stat.state = state;
    memcpy(param.conn_stat.remote_bda, PEER_BDA, sizeof(PEER_BDA));
    param.conn_stat.conn_hdl = HANDLE;
    bt_events_handle_a2dp_connection(&param);
}

static void send_audio(esp_a2d_audio_state_t state)
{
    esp_a2d_cb_param_t param = {0};
    param.audio_stat.state = state;
    memcpy(param.audio_stat.remote_bda, PEER_BDA, sizeof(PEER_BDA));
    param.audio_stat.conn_hdl = HANDLE;
    bt_events_handle_a2dp_audio(&param);
}

static esp_a2d_cb_param_t audio_param(esp_a2d_audio_state_t state)
{
    esp_a2d_cb_param_t param = {0};
    param.audio_stat.state = state;
    memcpy(param.audio_stat.remote_bda, PEER_BDA, sizeof(PEER_BDA));
    param.audio_stat.conn_hdl = HANDLE;
    return param;
}

static bt_events_a2dp_binding_snapshot_t binding_snapshot(void)
{
    bt_events_a2dp_binding_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&snapshot));
    return snapshot;
}

static void assert_binding_equal(
    const bt_events_a2dp_binding_snapshot_t *expected,
    const bt_events_a2dp_binding_snapshot_t *actual)
{
    TEST_ASSERT_EQUAL(expected->valid, actual->valid);
    TEST_ASSERT_EQUAL_STRING(expected->peer_mac, actual->peer_mac);
    TEST_ASSERT_EQUAL_UINT(expected->conn_handle, actual->conn_handle);
    TEST_ASSERT_EQUAL_UINT32(expected->lifecycle_serial,
                             actual->lifecycle_serial);
    TEST_ASSERT_EQUAL_UINT32(expected->last_duplex_generation,
                             actual->last_duplex_generation);
    TEST_ASSERT_EQUAL_UINT64(expected->missing_binding_rejections,
                             actual->missing_binding_rejections);
    TEST_ASSERT_EQUAL_UINT64(expected->wrong_peer_rejections,
                             actual->wrong_peer_rejections);
    TEST_ASSERT_EQUAL_UINT64(expected->stale_handle_rejections,
                             actual->stale_handle_rejections);
    TEST_ASSERT_EQUAL_UINT64(expected->generation_sync_failures,
                             actual->generation_sync_failures);
    TEST_ASSERT_EQUAL_UINT64(expected->late_terminal_events_ignored,
                             actual->late_terminal_events_ignored);
}

static void establish_binding(void)
{
    bt_manager_test_set_hfp_policy_status(false, NULL, 0U, ESP_OK);
    bt_manager_test_set_hfp_policy_results(ESP_OK, ESP_OK);
    bt_manager_test_set_hfp_profile_created_generation(41U);
    send_connection(ESP_A2D_CONNECTION_STATE_CONNECTED);

    const bt_events_a2dp_binding_snapshot_t snapshot = binding_snapshot();
    TEST_ASSERT_TRUE(snapshot.valid);
    TEST_ASSERT_EQUAL_STRING(PEER, snapshot.peer_mac);
    TEST_ASSERT_EQUAL_UINT(HANDLE, snapshot.conn_handle);
    TEST_ASSERT_EQUAL_UINT32(41U, snapshot.last_duplex_generation);
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
    bt_events_a2dp_test_reset_secondary_errors();
    establish_binding();
}

void tearDown(void)
{
    bt_manager_test_deinit_mutex();
}

static void test_generation_diagnostic_lock_failure_preserves_primary_error(void)
{
    const bt_events_a2dp_binding_snapshot_t before = binding_snapshot();
    bt_manager_test_set_hfp_policy_status(
        true, PEER, 41U, ESP_ERR_NOT_SUPPORTED);
    bt_manager_test_force_next_ctx_lock_result(ESP_ERR_TIMEOUT);

    uint32_t generation = before.last_duplex_generation;
    TEST_ASSERT_EQUAL(
        ESP_ERR_NOT_SUPPORTED,
        bt_events_a2dp_test_refresh_bound_generation(
            PEER,
            HANDLE,
            before.lifecycle_serial,
            true,
            before.last_duplex_generation,
            &generation));
    TEST_ASSERT_EQUAL(
        ESP_ERR_TIMEOUT,
        bt_events_a2dp_test_get_last_generation_diag_update_error());
    TEST_ASSERT_EQUAL_UINT32(before.last_duplex_generation, generation);

    const bt_events_a2dp_binding_snapshot_t after = binding_snapshot();
    assert_binding_equal(&before, &after);
}

static void test_clear_helper_returns_exact_lock_error_without_mutation(void)
{
    const bt_events_a2dp_binding_snapshot_t before = binding_snapshot();
    bt_manager_test_force_next_ctx_lock_result(ESP_ERR_TIMEOUT);

    TEST_ASSERT_EQUAL(
        ESP_ERR_TIMEOUT,
        bt_events_a2dp_test_clear_binding_if_identity(
            before.lifecycle_serial, before.conn_handle));

    const bt_events_a2dp_binding_snapshot_t after = binding_snapshot();
    assert_binding_equal(&before, &after);
}

static void test_disconnect_clear_lock_failure_is_visible_and_preserves_binding(void)
{
    const bt_events_a2dp_binding_snapshot_t before = binding_snapshot();
    bt_manager_test_set_hfp_policy_status(true, PEER, 41U, ESP_OK);
    bt_events_a2dp_test_reset_secondary_errors();

    /* Disconnect processing locks once for identity/base-state commit, once for
     * generation refresh, then once for the binding clear. Fail only the third. */
    bt_manager_test_force_ctx_lock_after_successes(2U, ESP_ERR_TIMEOUT);
    send_connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);

    TEST_ASSERT_EQUAL(
        ESP_ERR_TIMEOUT,
        bt_events_a2dp_test_get_last_binding_clear_error());
    const bt_events_a2dp_binding_snapshot_t after = binding_snapshot();
    assert_binding_equal(&before, &after);
}

static void test_late_unbound_terminals_never_request_generation_refresh(void)
{
    bt_manager_test_set_hfp_policy_status(true, PEER, 41U, ESP_OK);
    send_connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);
    const bt_events_a2dp_binding_snapshot_t baseline = binding_snapshot();
    TEST_ASSERT_FALSE(baseline.valid);
    TEST_ASSERT_FALSE(bt_ctx.audio_playing);

    const unsigned status_before = bt_manager_test_get_hfp_status_calls();
    const unsigned policy_before = bt_manager_test_get_hfp_audio_policy_calls();
    const int callback_before = bt_manager_test_get_last_audio_state();

    send_audio(ESP_A2D_AUDIO_STATE_STOPPED);
    const bt_events_a2dp_binding_snapshot_t after_stop = binding_snapshot();
    TEST_ASSERT_EQUAL_UINT(status_before,
                           bt_manager_test_get_hfp_status_calls());
    TEST_ASSERT_EQUAL_UINT(policy_before,
                           bt_manager_test_get_hfp_audio_policy_calls());
    TEST_ASSERT_EQUAL_INT(callback_before,
                          bt_manager_test_get_last_audio_state());
    TEST_ASSERT_EQUAL_UINT64(baseline.late_terminal_events_ignored + 1U,
                             after_stop.late_terminal_events_ignored);
    TEST_ASSERT_EQUAL_UINT64(baseline.missing_binding_rejections,
                             after_stop.missing_binding_rejections);
    TEST_ASSERT_FALSE(bt_ctx.audio_playing);

    send_audio(ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND);
    const bt_events_a2dp_binding_snapshot_t after_suspend = binding_snapshot();
    TEST_ASSERT_EQUAL_UINT(status_before,
                           bt_manager_test_get_hfp_status_calls());
    TEST_ASSERT_EQUAL_UINT(policy_before,
                           bt_manager_test_get_hfp_audio_policy_calls());
    TEST_ASSERT_EQUAL_INT(callback_before,
                          bt_manager_test_get_last_audio_state());
    TEST_ASSERT_EQUAL_UINT64(after_stop.late_terminal_events_ignored + 1U,
                             after_suspend.late_terminal_events_ignored);
    TEST_ASSERT_EQUAL_UINT64(after_stop.missing_binding_rejections,
                             after_suspend.missing_binding_rejections);
    TEST_ASSERT_FALSE(bt_ctx.audio_playing);
}

static void test_unbound_start_prepare_path_returns_exact_invalid_state(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_events_a2dp_test_reset_binding());
    const bt_events_a2dp_binding_snapshot_t baseline = binding_snapshot();
    const unsigned policy_before = bt_manager_test_get_hfp_audio_policy_calls();
    const int callback_before = bt_manager_test_get_last_audio_state();
    const bool audio_before = bt_ctx.audio_playing;
    esp_a2d_cb_param_t param = audio_param(ESP_A2D_AUDIO_STATE_STARTED);

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        bt_events_a2dp_test_prepare_audio_event(&param));

    const bt_events_a2dp_binding_snapshot_t after = binding_snapshot();
    TEST_ASSERT_EQUAL_UINT64(baseline.missing_binding_rejections + 1U,
                             after.missing_binding_rejections);
    TEST_ASSERT_EQUAL_UINT64(baseline.late_terminal_events_ignored,
                             after.late_terminal_events_ignored);
    TEST_ASSERT_EQUAL_UINT(policy_before,
                           bt_manager_test_get_hfp_audio_policy_calls());
    TEST_ASSERT_EQUAL_INT(callback_before,
                          bt_manager_test_get_last_audio_state());
    TEST_ASSERT_EQUAL(audio_before, bt_ctx.audio_playing);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_generation_diagnostic_lock_failure_preserves_primary_error);
    RUN_TEST(test_clear_helper_returns_exact_lock_error_without_mutation);
    RUN_TEST(test_disconnect_clear_lock_failure_is_visible_and_preserves_binding);
    RUN_TEST(test_late_unbound_terminals_never_request_generation_refresh);
    RUN_TEST(test_unbound_start_prepare_path_returns_exact_invalid_state);
    return UNITY_END();
}
