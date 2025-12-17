#include "unity.h"
#include "esp_err.h"
#include "esp_bt.h"
#include "bt_source.h"
#include "mock_a2dp.h"

/* Forward declare public connection manager APIs not covered by headers. */
esp_err_t bt_register_connection_callback(bt_connection_callback_t callback, void *user_data);
esp_err_t bt_register_streaming_callback(bt_stream_callback_t callback, void *user_data);
void bt_connection_state_cb(esp_a2d_connection_state_t state, esp_bd_addr_t bd_addr);
void bt_audio_state_cb(esp_a2d_audio_state_t state, esp_bd_addr_t bd_addr);

// Forward declarations for UNIT_TEST helpers in bt_connection_manager.c
void bt_connection_manager_reset_state_for_test(void);
void bt_connection_manager_set_auto_reconnect_for_test(bool enable);
const char *bt_connection_manager_get_last_connected_addr_for_test(void);
uint8_t bt_connection_manager_get_reconnect_attempts_for_test(void);

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
    void *user_data;
} test_connection_ctx_t;

static void test_connection_callback(bt_connection_info_t *info, void *user_data)
{
    test_connection_ctx_t *ctx = (test_connection_ctx_t *)user_data;
    ctx->invoked = true;
    ctx->info = *info;
}

typedef struct {
    bool invoked;
    bool streaming_flag;
    bt_streaming_info_t info;
    void *user_data;
} test_stream_ctx_t;

static void test_stream_callback(bool streaming, const bt_streaming_info_t *info, void *user_data)
{
    test_stream_ctx_t *ctx = (test_stream_ctx_t *)user_data;
    ctx->invoked = true;
    ctx->streaming_flag = streaming;
    ctx->info = *info;
}

void setUp(void)
{
    mock_a2dp_reset();
    bt_connection_manager_reset_state_for_test();
}

void tearDown(void)
{
    mock_a2dp_reset();
}

static void init_connection_manager(void)
{
    bt_connection_manager_init(dummy_a2d_event_cb, dummy_data_cb);
}

void test_bt_connection_manager_init_registers_callbacks(void)
{
    init_connection_manager();

    TEST_ASSERT_EQUAL_PTR(dummy_a2d_event_cb, mock_a2dp_get_registered_callback());
    TEST_ASSERT_EQUAL_PTR(dummy_data_cb, mock_a2dp_get_registered_data_callback());
}

void test_bt_connection_state_updates_and_notifies(void)
{
    init_connection_manager();

    test_connection_ctx_t conn_ctx = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_register_connection_callback(test_connection_callback, &conn_ctx));

    esp_bd_addr_t addr = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_CONNECTED, addr);

    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_CONNECTED, bt_get_connection_state_detailed());
    TEST_ASSERT_TRUE(conn_ctx.invoked);
    TEST_ASSERT_TRUE(conn_ctx.info.connected);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", conn_ctx.info.addr);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_CONNECTED, conn_ctx.info.state);

    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_DISCONNECTING, addr);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_DISCONNECTING, bt_get_connection_state_detailed());
}

void test_bt_connection_manager_attempts_reconnect_on_disconnect(void)
{
    init_connection_manager();
    bt_connection_manager_set_auto_reconnect_for_test(true);

    esp_bd_addr_t addr = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_CONNECTED, addr);
    TEST_ASSERT_EQUAL_STRING("01:02:03:04:05:06", bt_connection_manager_get_last_connected_addr_for_test());

    TEST_ASSERT_EQUAL(0, mock_a2dp_get_connect_calls());
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_DISCONNECTED, addr);

    TEST_ASSERT_EQUAL(1, mock_a2dp_get_connect_calls());
    TEST_ASSERT_EQUAL_STRING("01:02:03:04:05:06", mock_a2dp_get_last_connect_addr());
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_CONNECTING, bt_get_connection_state_detailed());

    bt_connection_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info));
    TEST_ASSERT_EQUAL_UINT8(1, info.retry_count);
    TEST_ASSERT_EQUAL_UINT8(1, bt_connection_manager_get_reconnect_attempts_for_test());
}

void test_bt_connection_manager_limits_reconnect_attempts(void)
{
    init_connection_manager();
    bt_connection_manager_set_auto_reconnect_for_test(true);

    // Force connect attempts to fail so the manager exercises its retry loop
    mock_a2dp_set_connect_result(ESP_BT_STATUS_FAIL);

    esp_bd_addr_t addr = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_CONNECTED, addr);
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_DISCONNECTED, addr);

    TEST_ASSERT_EQUAL(5, mock_a2dp_get_connect_calls());
    TEST_ASSERT_EQUAL_UINT8(5, bt_connection_manager_get_reconnect_attempts_for_test());
}

void test_bt_connection_manager_reconnect_resets_between_disconnects(void)
{
    init_connection_manager();
    bt_connection_manager_set_auto_reconnect_for_test(true);

    esp_bd_addr_t addr = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};

    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_CONNECTED, addr);
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_DISCONNECTED, addr);
    TEST_ASSERT_EQUAL(1, mock_a2dp_get_connect_calls());
    TEST_ASSERT_EQUAL_UINT8(1, bt_connection_manager_get_reconnect_attempts_for_test());

    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_DISCONNECTED, addr);
    TEST_ASSERT_EQUAL(2, mock_a2dp_get_connect_calls());
    TEST_ASSERT_EQUAL_UINT8(1, bt_connection_manager_get_reconnect_attempts_for_test());
}

void test_bt_connection_manager_skips_reconnect_when_disabled(void)
{
    init_connection_manager();
    bt_connection_manager_set_auto_reconnect_for_test(false);

    esp_bd_addr_t addr = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26};

    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_CONNECTED, addr);
    bt_connection_state_cb(ESP_A2D_CONNECTION_STATE_DISCONNECTED, addr);

    TEST_ASSERT_EQUAL(0, mock_a2dp_get_connect_calls());
    TEST_ASSERT_EQUAL_UINT8(0, bt_connection_manager_get_reconnect_attempts_for_test());
}

void test_bt_audio_state_transitions_and_notifies(void)
{
    init_connection_manager();

    test_stream_ctx_t stream_ctx = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_register_streaming_callback(test_stream_callback, &stream_ctx));

    esp_bd_addr_t addr = {0};

    bt_audio_state_cb(ESP_A2D_AUDIO_STATE_STARTED, addr);
    TEST_ASSERT_TRUE(stream_ctx.invoked);
    TEST_ASSERT_TRUE(stream_ctx.streaming_flag);
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STREAMING, bt_get_streaming_state());

    stream_ctx.invoked = false;
    bt_audio_state_cb(ESP_A2D_AUDIO_STATE_STOPPED, addr);
    TEST_ASSERT_TRUE(stream_ctx.invoked);
    TEST_ASSERT_FALSE(stream_ctx.streaming_flag);
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_PAUSED, bt_get_streaming_state());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bt_connection_manager_init_registers_callbacks);
    RUN_TEST(test_bt_connection_state_updates_and_notifies);
    RUN_TEST(test_bt_connection_manager_attempts_reconnect_on_disconnect);
    RUN_TEST(test_bt_connection_manager_limits_reconnect_attempts);
    RUN_TEST(test_bt_connection_manager_reconnect_resets_between_disconnects);
    RUN_TEST(test_bt_connection_manager_skips_reconnect_when_disabled);
    RUN_TEST(test_bt_audio_state_transitions_and_notifies);
    return UNITY_END();
}
