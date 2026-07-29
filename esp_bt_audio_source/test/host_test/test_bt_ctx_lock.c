/**
 * @file test_bt_ctx_lock.c
 * @brief Unit tests for bt_ctx synchronization and init transactionality
 */

#include <string.h>
#include "unity.h"
#include "bt_manager.h"
#include "bt_manager_internal.h"
#include "bt_events_a2dp.h"
#include "platform_sync.h"
#include "esp_err.h"

typedef struct {
    bool initialized;
    bool connected;
    bool audio_playing;
    bool scanning;
    char connected_mac[18];
    char connected_name[32];
} bt_manager_status_t;

esp_err_t bt_manager_get_status(bt_manager_status_t *status);

extern void bt_manager_test_reset_btstate_mock(void);
extern void bt_manager_test_set_hfp_policy_status(bool peer_valid,
                                                   const char *peer,
                                                   uint32_t generation,
                                                   esp_err_t result);
extern void bt_manager_test_set_hfp_policy_results(esp_err_t profile_result,
                                                    esp_err_t audio_result);
extern void bt_manager_test_set_hfp_profile_created_generation(
    uint32_t generation);

static const uint8_t TEST_PEER_BDA[6] = {
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
};
static const char *TEST_PEER = "aa:bb:cc:dd:ee:ff";
static bool s_skip_teardown;

static bt_manager_init_t test_config(void)
{
    bt_manager_init_t config = {
        .device_name = "TestDevice",
        .connected_cb = NULL,
        .disconnected_cb = NULL,
    };
    return config;
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

static void establish_bound_session(void)
{
    bt_manager_test_set_hfp_policy_status(false, NULL, 0U, ESP_OK);
    bt_manager_test_set_hfp_policy_results(ESP_OK, ESP_OK);
    bt_manager_test_set_hfp_profile_created_generation(41U);

    esp_a2d_cb_param_t param = {0};
    param.conn_stat.state = ESP_A2D_CONNECTION_STATE_CONNECTED;
    memcpy(param.conn_stat.remote_bda, TEST_PEER_BDA,
           sizeof(TEST_PEER_BDA));
    param.conn_stat.conn_hdl = 7;
    bt_events_handle_a2dp_connection(&param);

    bt_events_a2dp_binding_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&snapshot));
    TEST_ASSERT_TRUE(snapshot.valid);
    TEST_ASSERT_EQUAL_STRING(TEST_PEER, snapshot.peer_mac);
    TEST_ASSERT_EQUAL_UINT(7U, snapshot.conn_handle);
    TEST_ASSERT_EQUAL_UINT32(41U, snapshot.last_duplex_generation);
}

void setUp(void)
{
    s_skip_teardown = false;
    bt_manager_test_reset_btstate_mock();
    bt_manager_init_t config = test_config();
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_init(&config));
}

void tearDown(void)
{
    if (s_skip_teardown) {
        return;
    }
    if (bt_ctx.initialized) {
        TEST_ASSERT_EQUAL(ESP_OK, bt_manager_deinit());
    } else {
        bt_manager_test_deinit_mutex();
    }
}

void test_bt_ctx_lock_should_succeed_after_init(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_ctx_lock(PLATFORM_WAIT_FOREVER));
    bt_ctx_unlock();
}

void test_bt_ctx_lock_should_fail_before_init(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_deinit());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_ctx_lock(PLATFORM_WAIT_FOREVER));
}

void test_bt_ctx_unlock_should_succeed(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_ctx_lock(PLATFORM_WAIT_FOREVER));
    bt_ctx_unlock();
    TEST_PASS();
}

void test_bt_manager_get_status_returns_coherent_snapshot(void)
{
    bt_ctx.connected = true;
    strcpy(bt_ctx.connected_mac, "A0:BB:CC:DD:EE:FF");
    strcpy(bt_ctx.connected_name, "Test");

    bt_manager_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_get_status(&status));
    TEST_ASSERT_TRUE(status.connected);
}

void test_bt_manager_get_status_with_null_output(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, bt_manager_get_status(NULL));
}

void test_bt_ctx_lock_does_not_deadlock_on_reentry(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_ctx_lock(PLATFORM_WAIT_FOREVER));
    bt_ctx_unlock();
    TEST_ASSERT_EQUAL(ESP_OK, bt_ctx_lock(PLATFORM_WAIT_FOREVER));
    bt_ctx_unlock();
}

void test_bt_ctx_lock_multiple_cycles(void)
{
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, bt_ctx_lock(PLATFORM_WAIT_FOREVER));
        bt_ctx_unlock();
    }
}

void test_bt_manager_init_reset_failure_is_exact_and_transactional(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_deinit());
    TEST_ASSERT_FALSE(bt_ctx.initialized);

    bt_manager_test_force_next_ctx_lock_result(ESP_ERR_TIMEOUT);
    bt_manager_init_t config = test_config();
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, bt_manager_init(&config));
    TEST_ASSERT_FALSE(bt_ctx.initialized);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_ctx_lock(PLATFORM_WAIT_FOREVER));

    /* A failed pre-callback reset is a clean, retryable init failure. */
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_init(&config));
    TEST_ASSERT_TRUE(bt_ctx.initialized);
}

void test_confirmed_shutdown_reset_failure_preserves_state_and_quarantines(void)
{
    establish_bound_session();
    bt_ctx.connected = true;
    bt_ctx.audio_playing = true;

    bt_events_a2dp_binding_snapshot_t before;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&before));

    bt_manager_test_force_next_ctx_lock_result(ESP_ERR_TIMEOUT);
    s_skip_teardown = true;
    TEST_ASSERT_EQUAL(
        ESP_ERR_TIMEOUT,
        bt_manager_test_finalize_teardown(ESP_OK, true));

    TEST_ASSERT_TRUE(bt_manager_test_is_quarantined());
    TEST_ASSERT_TRUE(bt_ctx.initialized);
    TEST_ASSERT_TRUE(bt_ctx.connected);
    TEST_ASSERT_TRUE(bt_ctx.audio_playing);

    bt_events_a2dp_binding_snapshot_t after;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&after));
    assert_binding_equal(&before, &after);

    /* The reset failure must retain the live mutex rather than deleting it. */
    TEST_ASSERT_EQUAL(ESP_OK, bt_ctx_lock(PLATFORM_WAIT_FOREVER));
    bt_ctx_unlock();

    bt_manager_init_t config = test_config();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bt_manager_init(&config));

    /* The process is ending and has no real callbacks. Release host resources
     * after proving the production finalizer retained them. */
    bt_manager_test_deinit_mutex();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bt_ctx_lock_should_succeed_after_init);
    RUN_TEST(test_bt_ctx_lock_should_fail_before_init);
    RUN_TEST(test_bt_ctx_unlock_should_succeed);
    RUN_TEST(test_bt_manager_get_status_returns_coherent_snapshot);
    RUN_TEST(test_bt_manager_get_status_with_null_output);
    RUN_TEST(test_bt_ctx_lock_does_not_deadlock_on_reentry);
    RUN_TEST(test_bt_ctx_lock_multiple_cycles);
    RUN_TEST(test_bt_manager_init_reset_failure_is_exact_and_transactional);

    /* Quarantine is intentionally irreversible until reboot. Keep this last. */
    RUN_TEST(
        test_confirmed_shutdown_reset_failure_preserves_state_and_quarantines);
    return UNITY_END();
}
