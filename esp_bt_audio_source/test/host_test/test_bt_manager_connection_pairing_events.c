#include <string.h>
#include "unity.h"
#include "bt_manager.h"
#include "bt_manager_internal.h"
#include "bt_connection.h"
#include "bt_events_gap.h"
#include "bt_events_a2dp.h"
#include "bt_events_avrc.h"
#include "mock_a2dp.h"
#include "mock_avrc.h"
#include "mock_gap.h"

extern void nvs_storage_mock_reset(void);
extern void nvs_storage_mock_set_remove_paired_device_result(esp_err_t err);
extern void nvs_storage_mock_set_clear_paired_devices_result(esp_err_t err);
extern bool nvs_storage_mock_was_clear_paired_devices_called(void);

extern void bt_manager_test_reset_forces(void);
extern void bt_manager_test_set_force_unpair_failure(int v);
extern void bt_manager_test_set_force_unpair_all_failure(int v);
extern const char* bt_manager_test_get_last_unpair_mac(void);

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

static const uint8_t PEER_A_BDA[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
static const uint8_t PEER_B_BDA[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
static const char *PEER_A = "aa:bb:cc:dd:ee:ff";
static const char *PEER_B = "11:22:33:44:55:66";
static bool s_skip_teardown;

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

static void establish_bound_session(const uint8_t bda[6], const char *peer,
                                    esp_a2d_conn_hdl_t handle,
                                    uint32_t generation)
{
    bt_manager_test_set_hfp_policy_status(false, NULL, 0U, ESP_OK);
    bt_manager_test_set_hfp_policy_results(ESP_OK, ESP_OK);
    bt_manager_test_set_hfp_profile_created_generation(generation);
    send_connection(ESP_A2D_CONNECTION_STATE_CONNECTED, bda, handle);

    bt_events_a2dp_binding_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&snapshot));
    TEST_ASSERT_TRUE(snapshot.valid);
    TEST_ASSERT_EQUAL_STRING(peer, snapshot.peer_mac);
    TEST_ASSERT_EQUAL_UINT(handle, snapshot.conn_handle);
    TEST_ASSERT_EQUAL_UINT32(generation,
                             snapshot.last_duplex_generation);
}

void setUp(void)
{
    s_skip_teardown = false;
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
    if (s_skip_teardown) return;
    if (bt_ctx.initialized) {
        TEST_ASSERT_EQUAL(ESP_OK, bt_manager_deinit());
    } else {
        bt_manager_test_deinit_mutex();
    }
}

void test_bt_connect_invalid_mac_format_should_fail(void)
{
    bt_ctx.initialized = true;
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_connect("INVALID_MAC"));
    TEST_ASSERT_EQUAL(0, mock_a2dp_get_connect_calls());
}

void test_bt_connect_already_connected_should_fail(void)
{
    bt_ctx.initialized = true;
    bt_ctx.connected = true;
    strncpy(bt_ctx.connected_mac, "11:22:33:44:55:66",
            sizeof(bt_ctx.connected_mac) - 1U);
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_connect("AA:BB:CC:DD:EE:FF"));
    TEST_ASSERT_EQUAL(0, mock_a2dp_get_connect_calls());
}

void test_bt_connect_a2dp_connect_failure_should_propagate_error(void)
{
    bt_ctx.initialized = true;
    mock_a2dp_set_connect_result(ESP_BT_STATUS_FAIL);
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_connect("11:22:33:44:55:66"));
    TEST_ASSERT_EQUAL(1, mock_a2dp_get_connect_calls());
}

void test_bt_disconnect_not_connected_is_idempotent(void)
{
    bt_ctx.initialized = true;
    TEST_ASSERT_EQUAL(ESP_OK, bt_disconnect());
    TEST_ASSERT_EQUAL(0, mock_a2dp_get_disconnect_calls());
}

void test_bt_disconnect_a2dp_disconnect_failure_should_propagate_error(void)
{
    bt_ctx.initialized = true;
    bt_ctx.connected = true;
    strncpy(bt_ctx.connected_mac, "11:22:33:44:55:66",
            sizeof(bt_ctx.connected_mac) - 1U);
    mock_a2dp_set_disconnect_result(ESP_BT_STATUS_FAIL);
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_disconnect());
    TEST_ASSERT_EQUAL(1, mock_a2dp_get_disconnect_calls());
}

void test_bt_pair_invalid_mac_should_return_invalid_arg(void)
{
    bt_ctx.initialized = true;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, bt_pair("NOT_A_MAC"));
}

void test_bt_unpair_device_not_in_nvs_but_in_controller_should_warn(void)
{
    bt_ctx.initialized = true;
    nvs_storage_mock_set_remove_paired_device_result(ESP_ERR_NOT_FOUND);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,
                      bt_unpair("11:22:33:44:55:66"));
    TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:66",
                             bt_manager_test_get_last_unpair_mac());
}

void test_bt_unpair_device_in_nvs_but_controller_fails_should_propagate_error(void)
{
    bt_ctx.initialized = true;
    mock_gap_set_remove_bond_result(ESP_FAIL);
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_unpair("11:22:33:44:55:66"));
}

void test_bt_unpair_all_partial_controller_failure_should_continue(void)
{
    bt_ctx.initialized = true;
    mock_gap_set_bond_device_count(3);
    mock_gap_set_remove_bond_fail_at_index(1);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, bt_unpair_all());
}

void test_bt_unpair_all_nvs_clears_despite_controller_failure(void)
{
    bt_ctx.initialized = true;
    mock_gap_set_bond_device_count(2);
    mock_gap_set_remove_bond_result(ESP_FAIL);
    nvs_storage_mock_set_clear_paired_devices_result(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_unpair_all());
    TEST_ASSERT_TRUE(nvs_storage_mock_was_clear_paired_devices_called());
}

void test_bt_events_gap_callback_unexpected_event_should_not_crash(void)
{
    bt_ctx.initialized = true;
    esp_bt_gap_cb_param_t param = {0};
    bt_events_gap_callback((esp_bt_gap_cb_event_t)999, &param);
    TEST_PASS();
}

void test_bt_events_a2dp_callback_unexpected_event_should_not_crash(void)
{
    bt_ctx.initialized = true;
    esp_a2d_cb_param_t param = {0};
    bt_events_a2dp_callback((esp_a2d_cb_event_t)999, &param);
    TEST_PASS();
}

void test_bt_events_avrc_callback_unexpected_event_should_not_crash(void)
{
    bt_ctx.initialized = true;
    esp_avrc_ct_cb_param_t param = {0};
    bt_events_avrc_callback((esp_avrc_ct_cb_event_t)999, &param);
    TEST_PASS();
}

void test_events_before_init_should_be_safe(void)
{
    esp_bt_gap_cb_param_t gap_param = {0};
    bt_events_gap_callback(ESP_BT_GAP_DISC_RES_EVT, &gap_param);
    esp_a2d_cb_param_t a2dp_param = {0};
    bt_events_a2dp_callback(ESP_A2D_CONNECTION_STATE_EVT, &a2dp_param);
    esp_avrc_ct_cb_param_t avrc_param = {0};
    bt_events_avrc_callback(ESP_AVRC_CT_CONNECTION_STATE_EVT, &avrc_param);
    TEST_ASSERT_FALSE(bt_ctx.initialized);
}

void test_a2dp_checked_reset_clears_identity_and_all_diagnostics(void)
{
    const esp_a2d_conn_hdl_t handle = 7;
    establish_bound_session(PEER_A_BDA, PEER_A, handle, 41U);

    send_audio(ESP_A2D_AUDIO_STATE_STARTED, PEER_B_BDA, 8);
    send_audio(ESP_A2D_AUDIO_STATE_STARTED, PEER_A_BDA, 8);
    bt_manager_test_set_hfp_policy_status(true, PEER_A, 41U,
                                           ESP_ERR_TIMEOUT);
    send_audio(ESP_A2D_AUDIO_STATE_STARTED, PEER_A_BDA, handle);
    bt_manager_test_set_hfp_policy_status(true, PEER_A, 41U, ESP_OK);
    send_connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED, PEER_A_BDA,
                    handle);
    send_audio(ESP_A2D_AUDIO_STATE_STARTED, PEER_A_BDA, handle);
    send_audio(ESP_A2D_AUDIO_STATE_STOPPED, PEER_A_BDA, handle);

    bt_events_a2dp_binding_snapshot_t populated;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&populated));
    TEST_ASSERT_FALSE(populated.valid);
    TEST_ASSERT_TRUE(populated.missing_binding_rejections > 0U);
    TEST_ASSERT_TRUE(populated.wrong_peer_rejections > 0U);
    TEST_ASSERT_TRUE(populated.stale_handle_rejections > 0U);
    TEST_ASSERT_TRUE(populated.generation_sync_failures > 0U);
    TEST_ASSERT_TRUE(populated.late_terminal_events_ignored > 0U);

    TEST_ASSERT_EQUAL(ESP_OK, bt_events_a2dp_reset_binding());
    bt_events_a2dp_binding_snapshot_t cleared;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&cleared));
    TEST_ASSERT_FALSE(cleared.valid);
    TEST_ASSERT_EQUAL_STRING("", cleared.peer_mac);
    TEST_ASSERT_EQUAL_UINT(0U, cleared.conn_handle);
    TEST_ASSERT_EQUAL_UINT32(0U, cleared.lifecycle_serial);
    TEST_ASSERT_EQUAL_UINT32(0U, cleared.last_duplex_generation);
    TEST_ASSERT_EQUAL_UINT64(0U, cleared.missing_binding_rejections);
    TEST_ASSERT_EQUAL_UINT64(0U, cleared.wrong_peer_rejections);
    TEST_ASSERT_EQUAL_UINT64(0U, cleared.stale_handle_rejections);
    TEST_ASSERT_EQUAL_UINT64(0U, cleared.generation_sync_failures);
    TEST_ASSERT_EQUAL_UINT64(0U, cleared.late_terminal_events_ignored);
}

void test_a2dp_reset_lock_failure_returns_exact_error_and_preserves_state(void)
{
    establish_bound_session(PEER_A_BDA, PEER_A, 7, 41U);
    bt_events_a2dp_binding_snapshot_t before;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&before));

    bt_manager_test_force_next_ctx_lock_result(ESP_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, bt_events_a2dp_reset_binding());

    bt_events_a2dp_binding_snapshot_t after;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&after));
    assert_binding_equal(&before, &after);
}

void test_a2dp_reset_without_mutex_fails_without_mutating_state(void)
{
    establish_bound_session(PEER_A_BDA, PEER_A, 7, 41U);
    bt_events_a2dp_binding_snapshot_t before;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&before));

    bt_manager_test_deinit_mutex();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_events_a2dp_reset_binding());
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_test_init_mutex());

    bt_events_a2dp_binding_snapshot_t after;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&after));
    assert_binding_equal(&before, &after);
}

static void assert_late_terminal_is_ignored(esp_a2d_audio_state_t terminal)
{
    const esp_a2d_conn_hdl_t handle = 7;
    establish_bound_session(PEER_A_BDA, PEER_A, handle, 41U);
    send_audio(ESP_A2D_AUDIO_STATE_STARTED, PEER_A_BDA, handle);
    send_connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED, PEER_A_BDA,
                    handle);

    bt_events_a2dp_binding_snapshot_t before;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&before));
    unsigned policy_calls = bt_manager_test_get_hfp_audio_policy_calls();
    int last_audio_state = bt_manager_test_get_last_audio_state();
    unsigned stale_records = bt_manager_test_get_stale_operation_records();

    send_audio(terminal, PEER_A_BDA, handle);

    bt_events_a2dp_binding_snapshot_t after;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&after));
    TEST_ASSERT_FALSE(bt_ctx.audio_playing);
    TEST_ASSERT_EQUAL_INT(last_audio_state,
                          bt_manager_test_get_last_audio_state());
    TEST_ASSERT_EQUAL_UINT(policy_calls,
        bt_manager_test_get_hfp_audio_policy_calls());
    TEST_ASSERT_EQUAL_UINT(stale_records,
        bt_manager_test_get_stale_operation_records());
    TEST_ASSERT_EQUAL_UINT64(before.late_terminal_events_ignored + 1U,
                             after.late_terminal_events_ignored);
    TEST_ASSERT_EQUAL_UINT64(before.missing_binding_rejections,
                             after.missing_binding_rejections);
}

void test_a2dp_late_stopped_after_disconnect_is_ignored(void)
{
    assert_late_terminal_is_ignored(ESP_A2D_AUDIO_STATE_STOPPED);
}

void test_a2dp_late_suspend_after_disconnect_is_ignored(void)
{
    assert_late_terminal_is_ignored(ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND);
}

void test_a2dp_unbound_started_is_rejected_without_state_mutation(void)
{
    bt_ctx.audio_playing = true;
    bt_events_a2dp_binding_snapshot_t before;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&before));
    unsigned policy_calls = bt_manager_test_get_hfp_audio_policy_calls();
    int last_audio_state = bt_manager_test_get_last_audio_state();

    send_audio(ESP_A2D_AUDIO_STATE_STARTED, PEER_A_BDA, 7);

    bt_events_a2dp_binding_snapshot_t after;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&after));
    TEST_ASSERT_TRUE(bt_ctx.audio_playing);
    TEST_ASSERT_EQUAL_INT(last_audio_state,
                          bt_manager_test_get_last_audio_state());
    TEST_ASSERT_EQUAL_UINT(policy_calls,
        bt_manager_test_get_hfp_audio_policy_calls());
    TEST_ASSERT_EQUAL_UINT64(before.missing_binding_rejections + 1U,
                             after.missing_binding_rejections);
    TEST_ASSERT_EQUAL_UINT64(before.late_terminal_events_ignored,
                             after.late_terminal_events_ignored);
}

void test_a2dp_stale_terminal_from_prior_session_cannot_change_new_session(void)
{
    establish_bound_session(PEER_A_BDA, PEER_A, 7, 41U);
    send_audio(ESP_A2D_AUDIO_STATE_STARTED, PEER_A_BDA, 7);
    send_connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED, PEER_A_BDA, 7);

    establish_bound_session(PEER_B_BDA, PEER_B, 9, 42U);
    send_audio(ESP_A2D_AUDIO_STATE_STARTED, PEER_B_BDA, 9);
    TEST_ASSERT_TRUE(bt_ctx.audio_playing);

    bt_events_a2dp_binding_snapshot_t before;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&before));
    unsigned policy_calls = bt_manager_test_get_hfp_audio_policy_calls();
    int last_audio_state = bt_manager_test_get_last_audio_state();

    send_audio(ESP_A2D_AUDIO_STATE_STOPPED, PEER_A_BDA, 7);

    bt_events_a2dp_binding_snapshot_t after;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&after));
    TEST_ASSERT_TRUE(bt_ctx.audio_playing);
    TEST_ASSERT_TRUE(after.valid);
    TEST_ASSERT_EQUAL_STRING(PEER_B, after.peer_mac);
    TEST_ASSERT_EQUAL_UINT(9U, after.conn_handle);
    TEST_ASSERT_EQUAL_INT(last_audio_state,
                          bt_manager_test_get_last_audio_state());
    TEST_ASSERT_EQUAL_UINT(policy_calls,
        bt_manager_test_get_hfp_audio_policy_calls());
    TEST_ASSERT_EQUAL_UINT64(before.wrong_peer_rejections + 1U,
                             after.wrong_peer_rejections);
    TEST_ASSERT_EQUAL_UINT64(before.late_terminal_events_ignored,
                             after.late_terminal_events_ignored);
}

void test_clean_manager_deinit_init_cycle_starts_with_empty_binding(void)
{
    establish_bound_session(PEER_A_BDA, PEER_A, 7, 41U);
    bt_ctx.initialized = true;
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_deinit());

    bt_manager_init_t config = {
        .device_name = "A2DP-Lifecycle-Test",
        .connected_cb = NULL,
        .disconnected_cb = NULL,
    };
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_init(&config));

    bt_events_a2dp_binding_snapshot_t empty;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&empty));
    TEST_ASSERT_FALSE(empty.valid);
    TEST_ASSERT_EQUAL_STRING("", empty.peer_mac);
    TEST_ASSERT_EQUAL_UINT32(0U, empty.lifecycle_serial);
    TEST_ASSERT_EQUAL_UINT64(0U, empty.late_terminal_events_ignored);

    establish_bound_session(PEER_B_BDA, PEER_B, 9, 42U);
}

void test_unconfirmed_callback_shutdown_preserves_binding_and_quarantines(void)
{
    establish_bound_session(PEER_A_BDA, PEER_A, 7, 41U);
    bt_ctx.initialized = true;
    bt_ctx.connected = true;
    bt_ctx.audio_playing = true;

    bt_events_a2dp_binding_snapshot_t before;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&before));

    s_skip_teardown = true;
    TEST_ASSERT_EQUAL(ESP_FAIL,
        bt_manager_test_finalize_teardown(ESP_FAIL, false));
    TEST_ASSERT_TRUE(bt_manager_test_is_quarantined());
    TEST_ASSERT_TRUE(bt_ctx.initialized);
    TEST_ASSERT_TRUE(bt_ctx.connected);
    TEST_ASSERT_TRUE(bt_ctx.audio_playing);

    bt_events_a2dp_binding_snapshot_t after;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&after));
    assert_binding_equal(&before, &after);

    TEST_ASSERT_EQUAL(ESP_OK, bt_ctx_lock(PLATFORM_WAIT_FOREVER));
    bt_ctx_unlock();
    bt_manager_init_t config = {
        .device_name = "Must-Be-Rejected",
        .connected_cb = NULL,
        .disconnected_cb = NULL,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bt_manager_init(&config));

    /* The process is ending and has no real callbacks. Release the host-only
     * mutex after proving the production finalizer retained it. */
    bt_manager_test_deinit_mutex();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bt_connect_invalid_mac_format_should_fail);
    RUN_TEST(test_bt_connect_already_connected_should_fail);
    RUN_TEST(test_bt_connect_a2dp_connect_failure_should_propagate_error);
    RUN_TEST(test_bt_disconnect_not_connected_is_idempotent);
    RUN_TEST(test_bt_disconnect_a2dp_disconnect_failure_should_propagate_error);
    RUN_TEST(test_bt_pair_invalid_mac_should_return_invalid_arg);
    RUN_TEST(test_bt_unpair_device_not_in_nvs_but_in_controller_should_warn);
    RUN_TEST(test_bt_unpair_device_in_nvs_but_controller_fails_should_propagate_error);
    RUN_TEST(test_bt_unpair_all_partial_controller_failure_should_continue);
    RUN_TEST(test_bt_unpair_all_nvs_clears_despite_controller_failure);
    RUN_TEST(test_bt_events_gap_callback_unexpected_event_should_not_crash);
    RUN_TEST(test_bt_events_a2dp_callback_unexpected_event_should_not_crash);
    RUN_TEST(test_bt_events_avrc_callback_unexpected_event_should_not_crash);
    RUN_TEST(test_events_before_init_should_be_safe);

    RUN_TEST(test_a2dp_checked_reset_clears_identity_and_all_diagnostics);
    RUN_TEST(test_a2dp_reset_lock_failure_returns_exact_error_and_preserves_state);
    RUN_TEST(test_a2dp_reset_without_mutex_fails_without_mutating_state);
    RUN_TEST(test_a2dp_late_stopped_after_disconnect_is_ignored);
    RUN_TEST(test_a2dp_late_suspend_after_disconnect_is_ignored);
    RUN_TEST(test_a2dp_unbound_started_is_rejected_without_state_mutation);
    RUN_TEST(test_a2dp_stale_terminal_from_prior_session_cannot_change_new_session);
    RUN_TEST(test_clean_manager_deinit_init_cycle_starts_with_empty_binding);

    /* Quarantine is intentionally irreversible until reboot. Keep this last. */
    RUN_TEST(test_unconfirmed_callback_shutdown_preserves_binding_and_quarantines);
    return UNITY_END();
}
