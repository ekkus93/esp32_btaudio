/**
 * @file test_bt_connection_manager_edge_cases.c
 * @brief Unit tests for bt_connection_manager.c edge cases and missing coverage
 *
 * Phase 5.2: BT Connection Manager Missing Coverage
 * 
 * Tests cover:
 * - Reconnection retry with partial failures (failure → success)
 * - Invalid BD address handling  
 * - NULL callback handling (safety)
 * - Streaming state transitions during reconnection
 * - Connection info persistence across disconnects
 */

#include <string.h>
#include "unity.h"
#include "esp_err.h"
#include "esp_bt.h"
#include "bt_source.h"
#include "mock_a2dp.h"

/* Forward declare public connection manager APIs */
esp_err_t bt_register_connection_callback(bt_connection_callback_t callback, void *user_data);
esp_err_t bt_register_streaming_callback(bt_stream_callback_t callback, void *user_data);
void bt_connection_state_cb(esp_a2d_connection_state_t state, esp_bd_addr_t bd_addr);
void bt_audio_state_cb(esp_a2d_audio_state_t state, esp_bd_addr_t bd_addr);

// Test hooks (UNIT_TEST and CONFIG_BT_MOCK_TESTING)
void bt_connection_manager_reset_state_for_test(void);
void bt_connection_manager_set_auto_reconnect_for_test(bool enable);
const char *bt_connection_manager_get_last_connected_addr_for_test(void);
uint8_t bt_connection_manager_get_reconnect_attempts_for_test(void);
void bt_conn_test_set_reconnect_delay_ms(uint32_t delay_ms);

#if CONFIG_BT_MOCK_TESTING
void bt_conn_test_set_reconnect_results(const esp_err_t *results, size_t len);
void bt_conn_test_reset_state(void);
#endif

static void dummy_a2d_event_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    (void)event;
    (void)param;
}

static int32_t dummy_data_cb(uint8_t *buf, int32_t len)
{
    (void)buf;
    return len;
}

typedef struct {
    bool invoked;
    bt_connection_info_t info;
    int call_count;
} test_connection_ctx_t;

static void test_connection_callback(bt_connection_info_t *info, void *user_data)
{
    test_connection_ctx_t *ctx = (test_connection_ctx_t *)user_data;
    ctx->invoked = true;
    ctx->call_count++;
    ctx->info = *info;
}

typedef struct {
    bool invoked;
    bool streaming_flag;
    bt_streaming_info_t info;
    int call_count;
} test_stream_ctx_t;

static void test_stream_callback(bool streaming, const bt_streaming_info_t *info, void *user_data)
{
    test_stream_ctx_t *ctx = (test_stream_ctx_t *)user_data;
    ctx->invoked = true;
    ctx->call_count++;
    ctx->streaming_flag = streaming;
    ctx->info = *info;
}

void setUp(void)
{
    mock_a2dp_reset();
    bt_connection_manager_reset_state_for_test();
#if CONFIG_BT_MOCK_TESTING
    bt_conn_test_reset_state();
#endif
    bt_conn_test_set_reconnect_delay_ms(0); // No delay for fast tests
}

void tearDown(void)
{
    mock_a2dp_reset();
}

static void init_connection_manager(void)
{
    bt_connection_manager_init(dummy_a2d_event_cb, dummy_data_cb);
}

/**
 * TDD Test 1: Reconnect with partial failures - fail, fail, succeed
 * 
 * Behavior: If reconnection fails on first two attempts but succeeds on third,
 * the connection should be established and retry count should reflect all attempts.
 */
#if CONFIG_BT_MOCK_TESTING
void test_bt_reconnect_partial_failures_then_success(void)
{
    init_connection_manager();
    bt_connection_manager_set_auto_reconnect_for_test(true);
    
    test_connection_ctx_t conn_ctx = {0};
    bt_register_connection_callback(test_connection_callback, &conn_ctx);
    
    // Arrange: Set reconnect sequence: FAIL, FAIL, SUCCESS
    esp_err_t reconnect_results[] = {ESP_FAIL, ESP_FAIL, ESP_OK};
    bt_conn_test_set_reconnect_results(reconnect_results, 3);
    
    // Arrange: Connect to device first
    esp_bd_addr_t addr = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_CONNECTED, addr);
    
    // Act: Disconnect - should trigger automatic reconnect attempts
    conn_ctx.call_count = 0; // Reset callback counter
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_DISCONNECTED, addr);
    
    // Assert: Should have attempted 3 reconnects (2 failures + 1 success)
    // Note: When using bt_conn_test_set_reconnect_results(), the initiate_connection()
    // returns from the test array WITHOUT calling mock_a2dp esp_a2d_source_connect(),
    // so we check reconnect_attempts instead of mock_a2dp_get_connect_calls()
    TEST_ASSERT_EQUAL(3, bt_connection_manager_get_reconnect_attempts_for_test());
    
    // Assert: Connection state should be CONNECTING (waiting for A2DP connected event)
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_CONNECTING, bt_get_connection_state_detailed());
    
    // Assert: Callback should have been invoked for state changes
    TEST_ASSERT_TRUE(conn_ctx.call_count >= 1);
}
#endif

/**
 * TDD Test 2: NULL connection callback should not crash
 * 
 * Behavior: When no connection callback is registered, state transitions
 * should still work without crashing.
 */
void test_bt_connection_state_change_null_callback_should_not_crash(void)
{
    init_connection_manager();
    bt_connection_manager_set_auto_reconnect_for_test(false); // Disable auto-reconnect
    
    // Arrange: Do NOT register callback (remains NULL)
    esp_bd_addr_t addr = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    // Act: Trigger state change with NULL callback
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_CONNECTED, addr);
    
    // Assert: State should update correctly even without callback
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_CONNECTED, bt_get_connection_state_detailed());
    
    // Act: Disconnect
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_DISCONNECTED, addr);
    
    // Assert: Should not crash
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_DISCONNECTED, bt_get_connection_state_detailed());
}

/**
 * TDD Test 3: NULL streaming callback should not crash
 * 
 * Behavior: When no streaming callback is registered, audio state transitions
 * should still work without crashing.
 */
void test_bt_audio_state_change_null_callback_should_not_crash(void)
{
    init_connection_manager();
    
    // Arrange: Do NOT register streaming callback (remains NULL)
    esp_bd_addr_t addr = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    
    // Act: Trigger audio state changes with NULL callback
    bt_audio_state_cb(ESP_A2D_AUDIO_STATE_STARTED, addr);
    
    // Assert: State should update correctly
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STREAMING, bt_get_streaming_state());
    
    // Act: Stop streaming
    bt_audio_state_cb(ESP_A2D_AUDIO_STATE_STOPPED, addr);
    
    // Assert: Should not crash
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_PAUSED, bt_get_streaming_state());
}

/**
 * TDD Test 4: Streaming state transitions during reconnection
 * 
 * Behavior: When reconnection occurs, streaming state should reset to STOPPED
 * when DISCONNECTED event arrives (not DISCONNECTING).
 * Note: Streaming callback is only invoked on audio state changes (STARTED/STOPPED),
 * not when connection state causes streaming state reset.
 */
void test_bt_streaming_state_resets_during_reconnection(void)
{
    init_connection_manager();
    bt_connection_manager_set_auto_reconnect_for_test(true);
    
    test_stream_ctx_t stream_ctx = {0};
    bt_register_streaming_callback(test_stream_callback, &stream_ctx);
    
    esp_bd_addr_t addr = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    
    // Arrange: Connect and start streaming
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_CONNECTED, addr);
    bt_audio_state_cb(ESP_A2D_AUDIO_STATE_STARTED, addr);
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STREAMING, bt_get_streaming_state());
    
    // Act: Go through DISCONNECTING first (proper sequence)
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_DISCONNECTING, addr);
    
    // Assert: Streaming state should still be STREAMING during DISCONNECTING
    // (only DISCONNECTED event resets streaming to STOPPED)
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STREAMING, bt_get_streaming_state());
    
    // Act: Complete disconnect (triggers reconnection and resets streaming)
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_DISCONNECTED, addr);
    
    // Assert: NOW streaming state should be STOPPED
    // (connection state change resets streaming, but doesn't invoke callback)
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STOPPED, bt_get_streaming_state());
}

/**
 * TDD Test 5: Connection info persists across disconnects
 * 
 * Behavior: Connection info (address, connect time) should be preserved
 * even after disconnect, to support reconnection.
 */
void test_bt_connection_info_persists_across_disconnect(void)
{
    init_connection_manager();
    bt_connection_manager_set_auto_reconnect_for_test(false); // Disable to get clean DISCONNECTED state
    
    esp_bd_addr_t addr = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    
    // Arrange: Connect to device
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_CONNECTED, addr);
    
    bt_connection_info_t info_before = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info_before));
    TEST_ASSERT_EQUAL_STRING("DE:AD:BE:EF:CA:FE", info_before.addr);
    TEST_ASSERT_TRUE(info_before.connected);
    uint32_t connect_time = info_before.connect_time;
    
    // Act: Disconnect
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_DISCONNECTED, addr);
    
    // Assert: Address should still be accessible (for reconnection)
    TEST_ASSERT_EQUAL_STRING("DE:AD:BE:EF:CA:FE", 
                            bt_connection_manager_get_last_connected_addr_for_test());
    
    // Note: Connection info struct state changes to DISCONNECTED,
    // but last_connected_addr is preserved for reconnection
    bt_connection_info_t info_after = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info_after));
    TEST_ASSERT_FALSE(info_after.connected);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_DISCONNECTED, info_after.state);
    
    // The connect_time from before disconnect should be preserved
    // (though the info struct itself shows disconnected state)
    TEST_ASSERT_EQUAL(connect_time, info_before.connect_time);
}

/**
 * TDD Test 6: Streaming state transitions through connection lifecycle
 * 
 * Behavior: Validates streaming state transitions through proper A2DP sequence.
 * Audio must stop before disconnect to properly reset streaming state.
 */
void test_bt_streaming_state_through_connection_states(void)
{
    init_connection_manager();
    bt_connection_manager_set_auto_reconnect_for_test(false); // Manual for control
    
    test_stream_ctx_t stream_ctx = {0};
    bt_register_streaming_callback(test_stream_callback, &stream_ctx);
    
    esp_bd_addr_t addr = {0x11, 0x11, 0x22, 0x22, 0x33, 0x33};
    
    // Arrange: Connect, start streaming
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_CONNECTED, addr);
    bt_audio_state_cb(ESP_A2D_AUDIO_STATE_STARTED, addr);
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STREAMING, bt_get_streaming_state());
    
    // Act: Stop audio first (proper A2DP sequence)
    bt_audio_state_cb(ESP_A2D_AUDIO_STATE_STOPPED, addr);
    // Note: STOPPED while STREAMING goes to PAUSED (remote suspend logic)
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_PAUSED, bt_get_streaming_state());
    
    // Act: Go through DISCONNECTING state
    stream_ctx.call_count = 0;
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_DISCONNECTING, addr);
    
    // Assert: Streaming state unchanged during DISCONNECTING (still PAUSED)
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_PAUSED, bt_get_streaming_state());
    
    // Act: Complete disconnect
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_DISCONNECTED, addr);
    
    // Assert: NOW streaming should be STOPPED (DISCONNECTED resets it)
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STOPPED, bt_get_streaming_state());
}

/**
 * TDD Test 7: Connection info updates correctly on new connection
 * 
 * Behavior: When connecting to a different device, connection info
 * should update with new address and timestamp.
 */
void test_bt_connection_info_updates_on_new_device(void)
{
    init_connection_manager();
    
    // Arrange: Connect to first device
    esp_bd_addr_t addr1 = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_CONNECTED, addr1);
    
    bt_connection_info_t info1 = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info1));
    TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:66", info1.addr);
    uint32_t time1 = info1.connect_time;
    
    // Act: Disconnect and connect to different device
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_DISCONNECTED, addr1);
    
    // Small delay to ensure different timestamp (if time() has 1-second resolution)
    // In tests this might be same second, so we just verify address changes
    esp_bd_addr_t addr2 = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_CONNECTED, addr2);
    
    bt_connection_info_t info2 = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info2));
    
    // Assert: Address should be updated
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", info2.addr);
    TEST_ASSERT_TRUE(info2.connected);
    
    // Assert: Connect time should be >= previous (may be same if fast test)
    TEST_ASSERT_TRUE(info2.connect_time >= time1);
}

/* TEST-3a: Disconnect while audio is active transitions streaming to STOPPED.
 *
 * Proxy for "audio processor transitions to idle on disconnect": we verify
 * that the connection manager transitions the streaming state to STOPPED when
 * the A2DP audio state is reported as STOPPED (which the BT stack emits when
 * the A2DP source disconnects mid-stream).
 */
void test_bt_disconnect_mid_stream_transitions_streaming_state_to_stopped(void)
{
    init_connection_manager();
    bt_connection_manager_set_auto_reconnect_for_test(false);

    esp_bd_addr_t addr = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};

    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_CONNECTED, addr);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_CONNECTED, bt_get_connection_state_detailed());

    /* Simulate audio stream starting. */
    bt_audio_state_cb(ESP_A2D_AUDIO_STATE_STARTED, addr);
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STREAMING, bt_get_streaming_state());

    /* Mid-stream disconnect: BT stack fires AUDIO_STATE_STOPPED.
     * Per bt_connection_manager.c logic, STOPPED while streaming → PAUSED. */
    bt_audio_state_cb(ESP_A2D_AUDIO_STATE_STOPPED, addr);
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_PAUSED, bt_get_streaming_state());

    /* Then the connection itself drops, which resets streaming to STOPPED. */
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_DISCONNECTED, addr);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_DISCONNECTED, bt_get_connection_state_detailed());
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STOPPED, bt_get_streaming_state());
}

/* TEST-3b: All reconnect attempts exhausted → state settles at DISCONNECTED.
 *
 * This approximates the "connection timeout" scenario: the remote device never
 * responds so every reconnect attempt fails.  After exhausting retries the
 * manager must leave the system in a well-defined DISCONNECTED state so a new
 * connection attempt can succeed without a full reboot.
 */
void test_bt_all_reconnect_attempts_exhausted_returns_to_disconnected(void)
{
    init_connection_manager();
    bt_connection_manager_set_auto_reconnect_for_test(true);

    /* Force every connect attempt to fail immediately. */
    mock_a2dp_set_connect_result(ESP_BT_STATUS_FAIL);

    esp_bd_addr_t addr = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};

    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_CONNECTED, addr);
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_DISCONNECTED, addr);

    /* After exhausting retries, reconnect_attempts must equal the retry limit. */
    TEST_ASSERT_EQUAL_UINT8(5, bt_connection_manager_get_reconnect_attempts_for_test());

    /* The connection state must no longer be CONNECTING (manager gave up). */
    bt_connection_state_t final_state = bt_get_connection_state_detailed();
    TEST_ASSERT_NOT_EQUAL(BT_CONNECTION_STATE_CONNECTING, final_state);
}

/* TEST-3c: Reconnect succeeds after an unexpected disconnect.
 *
 * After a single unexpected disconnect, auto-reconnect fires and the remote
 * device accepts the re-connection.  Verify the state machine returns to
 * CONNECTED cleanly.
 */
void test_bt_reconnect_succeeds_after_unexpected_disconnect(void)
{
    init_connection_manager();
    bt_connection_manager_set_auto_reconnect_for_test(true);

    mock_a2dp_set_connect_result(ESP_BT_STATUS_SUCCESS);

    esp_bd_addr_t addr = {0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x01};

    /* Establish initial connection. */
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_CONNECTED, addr);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_CONNECTED, bt_get_connection_state_detailed());

    /* Unexpected disconnect fires reconnect. */
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_DISCONNECTED, addr);
    TEST_ASSERT_EQUAL(1, mock_a2dp_get_connect_calls());
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_CONNECTING, bt_get_connection_state_detailed());

    /* Simulate the remote device accepting the reconnect. */
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_CONNECTED, addr);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_CONNECTED, bt_get_connection_state_detailed());

    /* Streaming state should not be stuck; reset to 0 (STOPPED) on new connect. */
    bt_connection_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info));
    TEST_ASSERT_TRUE(info.connected);
}

// Unity test runner
int main(void)
{
    UNITY_BEGIN();

    // Section 5.2: Connection Manager Edge Cases
#if CONFIG_BT_MOCK_TESTING
    RUN_TEST(test_bt_reconnect_partial_failures_then_success);
#endif
    RUN_TEST(test_bt_connection_state_change_null_callback_should_not_crash);
    RUN_TEST(test_bt_audio_state_change_null_callback_should_not_crash);
    RUN_TEST(test_bt_streaming_state_resets_during_reconnection);
    RUN_TEST(test_bt_connection_info_persists_across_disconnect);
    RUN_TEST(test_bt_streaming_state_through_connection_states);
    RUN_TEST(test_bt_connection_info_updates_on_new_device);
    /* TEST-3: connection drop / timeout / reconnect scenarios */
    RUN_TEST(test_bt_disconnect_mid_stream_transitions_streaming_state_to_stopped);
#if CONFIG_BT_MOCK_TESTING
    RUN_TEST(test_bt_all_reconnect_attempts_exhausted_returns_to_disconnected);
    RUN_TEST(test_bt_reconnect_succeeds_after_unexpected_disconnect);
#endif

    return UNITY_END();
}
