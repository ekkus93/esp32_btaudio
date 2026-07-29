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

static const uint8_t PEER_BDA[6] = {
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
};
static const char *PEER = "aa:bb:cc:dd:ee:ff";
static const esp_a2d_conn_hdl_t HANDLE = 7;
static const esp_err_t INIT_ERROR = ESP_ERR_NOT_SUPPORTED;

static bt_manager_init_t test_config(void)
{
    bt_manager_init_t config = {
        .device_name = "RollbackTest",
        .connected_cb = NULL,
        .disconnected_cb = NULL,
    };
    return config;
}

static void send_connection(esp_a2d_connection_state_t state)
{
    esp_a2d_cb_param_t param = {0};
    param.conn_stat.state = state;
    memcpy(param.conn_stat.remote_bda, PEER_BDA, sizeof(PEER_BDA));
    param.conn_stat.conn_hdl = HANDLE;
    bt_events_handle_a2dp_connection(&param);
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
    establish_binding();
}

void tearDown(void)
{
    bt_manager_test_deinit_mutex();
}

static void test_complete_rollback_resets_before_mutex_delete(void)
{
    TEST_ASSERT_EQUAL(
        INIT_ERROR,
        bt_manager_test_finalize_init_rollback(
            INIT_ERROR, true, true, false));
    TEST_ASSERT_FALSE(bt_manager_test_is_quarantined());

    /* A successful guarded reset must have occurred before the mutex vanished. */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_ctx_lock(PLATFORM_WAIT_FOREVER));

    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_test_init_mutex());
    const bt_events_a2dp_binding_snapshot_t after = binding_snapshot();
    TEST_ASSERT_FALSE(after.valid);
    TEST_ASSERT_EQUAL_STRING("", after.peer_mac);
    TEST_ASSERT_EQUAL_UINT(0U, after.conn_handle);
    TEST_ASSERT_EQUAL_UINT32(0U, after.lifecycle_serial);
    TEST_ASSERT_EQUAL_UINT32(0U, after.last_duplex_generation);
}

static void test_unconfirmed_callback_shutdown_preserves_and_quarantines(void)
{
    const bt_events_a2dp_binding_snapshot_t before = binding_snapshot();

    TEST_ASSERT_EQUAL(
        INIT_ERROR,
        bt_manager_test_finalize_init_rollback(
            INIT_ERROR, false, true, false));
    TEST_ASSERT_TRUE(bt_manager_test_is_quarantined());

    const bt_events_a2dp_binding_snapshot_t after = binding_snapshot();
    assert_binding_equal(&before, &after);
    TEST_ASSERT_EQUAL(ESP_OK, bt_ctx_lock(PLATFORM_WAIT_FOREVER));
    bt_ctx_unlock();

    bt_manager_init_t config = test_config();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bt_manager_init(&config));
}

static void test_reset_lock_failure_preserves_primary_and_binding(void)
{
    const bt_events_a2dp_binding_snapshot_t before = binding_snapshot();
    bt_manager_test_force_next_ctx_lock_result(ESP_ERR_TIMEOUT);

    TEST_ASSERT_EQUAL(
        INIT_ERROR,
        bt_manager_test_finalize_init_rollback(
            INIT_ERROR, true, true, false));
    TEST_ASSERT_TRUE(bt_manager_test_is_quarantined());

    const bt_events_a2dp_binding_snapshot_t after = binding_snapshot();
    assert_binding_equal(&before, &after);
    TEST_ASSERT_EQUAL(ESP_OK, bt_ctx_lock(PLATFORM_WAIT_FOREVER));
    bt_ctx_unlock();

    bt_manager_init_t config = test_config();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bt_manager_init(&config));
}

static void test_prior_cleanup_failure_resets_safely_but_quarantines(void)
{
    TEST_ASSERT_EQUAL(
        INIT_ERROR,
        bt_manager_test_finalize_init_rollback(
            INIT_ERROR, true, false, false));
    TEST_ASSERT_TRUE(bt_manager_test_is_quarantined());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_ctx_lock(PLATFORM_WAIT_FOREVER));

    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_test_init_mutex());
    const bt_events_a2dp_binding_snapshot_t after = binding_snapshot();
    TEST_ASSERT_FALSE(after.valid);

    bt_manager_init_t config = test_config();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bt_manager_init(&config));
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    if (argc != 2) {
        TEST_FAIL_MESSAGE(
            "usage: test_bt_manager_init_rollback <complete|callbacks-live|reset-fails|cleanup-incomplete>");
    } else if (strcmp(argv[1], "complete") == 0) {
        RUN_TEST(test_complete_rollback_resets_before_mutex_delete);
    } else if (strcmp(argv[1], "callbacks-live") == 0) {
        RUN_TEST(test_unconfirmed_callback_shutdown_preserves_and_quarantines);
    } else if (strcmp(argv[1], "reset-fails") == 0) {
        RUN_TEST(test_reset_lock_failure_preserves_primary_and_binding);
    } else if (strcmp(argv[1], "cleanup-incomplete") == 0) {
        RUN_TEST(test_prior_cleanup_failure_resets_safely_but_quarantines);
    } else {
        TEST_FAIL_MESSAGE("unknown rollback test case");
    }
    return UNITY_END();
}
