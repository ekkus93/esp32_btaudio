#include "unity.h"

#include <string.h>

#include "bt_hfp_connection.h"
#include "bt_manager_internal.h"
#include "platform_sync.h"

#define PEER "AA:BB:CC:DD:EE:FF"
#define OTHER "11:22:33:44:55:66"

bt_manager_context_t bt_ctx;
bool s_autostart_enabled = true;

static platform_mutex_t s_manager_lock;
static esp_err_t s_connect_result;
static esp_err_t s_disconnect_result;
static unsigned s_connect_calls;
static unsigned s_disconnect_calls;

esp_err_t bt_ctx_lock(uint32_t timeout_ms)
{
    return platform_mutex_lock(s_manager_lock, timeout_ms);
}

void bt_ctx_unlock(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, platform_mutex_unlock(s_manager_lock));
}

esp_err_t bt_hfp_ag_get_snapshot(bt_hfp_ag_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->profile_ready = true;
    out->lifecycle = BT_HFP_AG_LIFECYCLE_READY;
    return ESP_OK;
}

static esp_err_t mock_connect(esp_bd_addr_t remote_bda)
{
    (void)remote_bda;
    s_connect_calls++;
    return s_connect_result;
}

static esp_err_t mock_disconnect(esp_bd_addr_t remote_bda)
{
    (void)remote_bda;
    s_disconnect_calls++;
    return s_disconnect_result;
}

static void manager_set(bool initialized, bool connected, const char *peer)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_ctx_lock(PLATFORM_WAIT_FOREVER));
    memset(&bt_ctx, 0, sizeof(bt_ctx));
    bt_ctx.initialized = initialized;
    bt_ctx.connected = connected;
    if (peer != NULL) {
        strncpy(bt_ctx.connected_mac, peer, sizeof(bt_ctx.connected_mac) - 1U);
    }
    bt_ctx_unlock();
}

void setUp(void)
{
    (void)bt_hfp_connection_cleanup_after_stack_shutdown();
    bt_duplex_state_deinit();
    if (s_manager_lock != NULL) {
        platform_mutex_delete(s_manager_lock);
    }
    s_manager_lock = platform_mutex_create();
    TEST_ASSERT_NOT_NULL(s_manager_lock);

    memset(&bt_ctx, 0, sizeof(bt_ctx));
    manager_set(true, true, PEER);
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_state_init());
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_init());

    s_connect_result = ESP_OK;
    s_disconnect_result = ESP_OK;
    s_connect_calls = 0U;
    s_disconnect_calls = 0U;
    const bt_hfp_connection_platform_ops_t ops = {
        .slc_connect = mock_connect,
        .slc_disconnect = mock_disconnect,
    };
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_hfp_connection_test_set_platform_ops(&ops));
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_hfp_connection_cleanup_after_stack_shutdown());
    bt_duplex_state_deinit();
    platform_mutex_delete(s_manager_lock);
    s_manager_lock = NULL;
}

void test_hfp_connect_rejects_invalid_mac(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, bt_hfp_connect("bad"));
    TEST_ASSERT_EQUAL_UINT(0, s_connect_calls);
}

void test_hfp_connect_rejects_uninitialized_manager(void)
{
    manager_set(false, false, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bt_hfp_connect(PEER));
    TEST_ASSERT_EQUAL_UINT(0, s_connect_calls);
}

void test_hfp_connect_accepts_same_active_peer_but_not_second_peer(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connect(PEER));
    TEST_ASSERT_EQUAL_UINT(1, s_connect_calls);

    bt_hfp_connection_snapshot_t operation;
    bt_duplex_snapshot_t duplex;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_get_snapshot(&operation));
    TEST_ASSERT_EQUAL(BT_HFP_OPERATION_REQUEST_SENT, operation.state);
    TEST_ASSERT_TRUE(operation.pending);
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_CONNECTING, duplex.hfp_profile_state);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, bt_hfp_connect(OTHER));
    TEST_ASSERT_EQUAL_UINT(1, s_connect_calls);
}

void test_hfp_connect_immediate_failure_is_returned_and_rolled_back(void)
{
    s_connect_result = ESP_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, bt_hfp_connect(PEER));

    bt_hfp_connection_snapshot_t operation;
    bt_duplex_snapshot_t duplex;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_get_snapshot(&operation));
    TEST_ASSERT_EQUAL(BT_HFP_OPERATION_REJECTED, operation.state);
    TEST_ASSERT_FALSE(operation.pending);
    TEST_ASSERT_EQUAL_UINT64(1, operation.immediate_failures);
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_DISCONNECTED,
                      duplex.hfp_profile_state);
}

void test_hfp_connect_completion_is_callback_confirmed_and_idempotent(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connect(PEER));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_handle_event(
        PEER, BT_HFP_AG_CONNECTION_SLC_CONNECTED));

    bt_hfp_connection_snapshot_t operation;
    bt_duplex_snapshot_t duplex;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_get_snapshot(&operation));
    TEST_ASSERT_EQUAL(BT_HFP_OPERATION_CONFIRMED, operation.state);
    TEST_ASSERT_FALSE(operation.pending);
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_SLC_CONNECTED,
                      duplex.hfp_profile_state);

    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connect(PEER));
    TEST_ASSERT_EQUAL_UINT(1, s_connect_calls);
}

void test_hfp_remote_rejection_is_asynchronous_and_visible(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connect(PEER));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_handle_event(
        PEER, BT_HFP_AG_CONNECTION_DISCONNECTED));

    bt_hfp_connection_snapshot_t operation;
    bt_duplex_snapshot_t duplex;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_get_snapshot(&operation));
    TEST_ASSERT_EQUAL(BT_HFP_OPERATION_REJECTED, operation.state);
    TEST_ASSERT_EQUAL_UINT64(1, operation.remote_rejections);
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_DISCONNECTED,
                      duplex.hfp_profile_state);
}

void test_hfp_timeout_does_not_fabricate_disconnect(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connect(PEER));
    bt_hfp_connection_test_expire_watchdog();

    bt_hfp_connection_snapshot_t operation;
    bt_duplex_snapshot_t duplex;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_get_snapshot(&operation));
    TEST_ASSERT_EQUAL(BT_HFP_OPERATION_TIMED_OUT, operation.state);
    TEST_ASSERT_FALSE(operation.pending);
    TEST_ASSERT_EQUAL_UINT64(1, operation.watchdog_timeouts);
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_CONNECTING,
                      duplex.hfp_profile_state);
    TEST_ASSERT_EQUAL(BT_AUDIO_HEALTH_DEGRADED, duplex.health);
}

void test_hfp_late_same_peer_event_after_new_generation_is_counted(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connect(PEER));
    bt_hfp_connection_test_expire_watchdog();

    bt_duplex_snapshot_t before;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&before));
    uint32_t new_generation = 0U;
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_session_begin(
        PEER, BT_DUPLEX_MODE_AUTO, &new_generation));
    TEST_ASSERT_NOT_EQUAL(before.session_generation, new_generation);

    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_handle_event(
        PEER, BT_HFP_AG_CONNECTION_SLC_CONNECTED));

    bt_hfp_connection_snapshot_t operation;
    bt_duplex_snapshot_t after;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_get_snapshot(&operation));
    TEST_ASSERT_EQUAL_UINT64(1, operation.stale_operation_events);
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&after));
    TEST_ASSERT_EQUAL_UINT64(1, after.counters.stale_generation_events);
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_DISCONNECTED,
                      after.hfp_profile_state);
}

void test_hfp_disconnect_uses_active_peer_and_requires_callback_confirmation(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connect(PEER));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_handle_event(
        PEER, BT_HFP_AG_CONNECTION_SLC_CONNECTED));

    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_disconnect());
    TEST_ASSERT_EQUAL_UINT(1, s_disconnect_calls);

    bt_hfp_connection_snapshot_t operation;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_get_snapshot(&operation));
    TEST_ASSERT_EQUAL(BT_HFP_OPERATION_REQUEST_SENT, operation.state);
    TEST_ASSERT_TRUE(operation.pending);

    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_handle_event(
        PEER, BT_HFP_AG_CONNECTION_DISCONNECTED));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_get_snapshot(&operation));
    TEST_ASSERT_EQUAL(BT_HFP_OPERATION_CONFIRMED, operation.state);
    TEST_ASSERT_FALSE(operation.pending);
}

void test_hfp_wrong_peer_event_is_ignored_and_counted(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connect(PEER));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_handle_event(
        OTHER, BT_HFP_AG_CONNECTION_SLC_CONNECTED));

    bt_hfp_connection_snapshot_t operation;
    bt_duplex_snapshot_t duplex;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_connection_get_snapshot(&operation));
    TEST_ASSERT_EQUAL_UINT64(1, operation.wrong_peer_events);
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_get_snapshot(&duplex));
    TEST_ASSERT_EQUAL_UINT64(1, duplex.counters.wrong_peer_events);
    TEST_ASSERT_EQUAL(BT_HFP_PROFILE_CONNECTING,
                      duplex.hfp_profile_state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hfp_connect_rejects_invalid_mac);
    RUN_TEST(test_hfp_connect_rejects_uninitialized_manager);
    RUN_TEST(test_hfp_connect_accepts_same_active_peer_but_not_second_peer);
    RUN_TEST(test_hfp_connect_immediate_failure_is_returned_and_rolled_back);
    RUN_TEST(test_hfp_connect_completion_is_callback_confirmed_and_idempotent);
    RUN_TEST(test_hfp_remote_rejection_is_asynchronous_and_visible);
    RUN_TEST(test_hfp_timeout_does_not_fabricate_disconnect);
    RUN_TEST(test_hfp_late_same_peer_event_after_new_generation_is_counted);
    RUN_TEST(test_hfp_disconnect_uses_active_peer_and_requires_callback_confirmation);
    RUN_TEST(test_hfp_wrong_peer_event_is_ignored_and_counted);
    return UNITY_END();
}
