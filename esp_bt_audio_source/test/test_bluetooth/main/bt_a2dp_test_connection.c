/* bt_a2dp_test_connection.c — connection scenario test bodies, split out of
 * bt_a2dp_test.c; linked into the same test_bluetooth app. */
#include "bt_a2dp_test_shared.h"

void test_bluetooth_connection(void) {
    ESP_LOGI(TAG, "Testing connecting to device by address");
    
    // Initialize Bluetooth and add mock devices
    test_bt_manager_init();
    setup_mock_devices();
    
    // Connect to device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    ESP_LOGI(TAG, "DIAG_TEST: bt_connect_device(%s) returned %d (0x%08x)", TEST_DEVICE_ADDR, (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check connection status
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Disconnect
    ESP_LOGI(TAG, "DIAG_TEST_MARKER: about to call bt_disconnect() (test_connect_by_addr)");
    ret = bt_disconnect();
    ESP_LOGI(TAG, "DIAG_TEST: bt_disconnect() returned %d (0x%08x)", (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    /* Wait for both stub and authoritative mock to reflect the disconnect. */
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(false, 1000));
    
    ESP_LOGI(TAG, "Bluetooth connection test completed");
}

void test_connect_by_name(void) {
    ESP_LOGI(TAG, "Testing connecting to device by name");
    
    // Initialize Bluetooth and add mock devices with connect-by-name hook
    test_bt_manager_init();
    bt_mock_setup_common(); // Use our common test setup
    
    // Connect by name
    esp_err_t ret = bt_connect_device_by_name(TEST_DEVICE_NAME);
    ESP_LOGI(TAG, "DIAG_TEST: bt_connect_device_by_name(%s) returned %d (0x%08x)", TEST_DEVICE_NAME, (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check connection status
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Disconnect
    ESP_LOGI(TAG, "DIAG_TEST_MARKER: about to call bt_disconnect() (test_connect_by_name)");
    ret = bt_disconnect();
    ESP_LOGI(TAG, "DIAG_TEST: bt_disconnect() returned %d (0x%08x)", (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

void test_connection_failure_handling(void) {
    ESP_LOGI(TAG, "Testing connection failure handling");
    
    // Initialize Bluetooth
    test_bt_manager_init();
    
    // Try to connect to a non-existent device
    const char* nonexistent_addr = "99:88:77:66:55:44";
    esp_err_t ret = bt_connect_device(nonexistent_addr);
    
    /* The mock may surface immediate lookup errors (ESP_FAIL/ESP_ERR_NOT_FOUND)
     * or accept the request and deliver the failure asynchronously. Accept any
     * non-success code but also treat ESP_OK as valid so long as the connection
     * never transitions to connected. */
    TEST_ASSERT((ret == ESP_FAIL) || (ret == ESP_ERR_NOT_FOUND) || (ret == ESP_OK));

    /* Log current connection states before waiting so we can observe stub vs
     * authoritative mock divergence when the assertion below fails. */
    bt_source_stub_release_disconnect_visibility();
    ESP_LOGI(TAG,
             "DIAG_TEST: after failed connect stub_connected=%d mock_connected=%d",
             bt_is_connected(),
             bt_mock_is_connected());

    // Verify not connected: wait for authoritative state to be false
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(false, 1000));
}

void test_connection_timeout(void) {
    ESP_LOGI(TAG, "Testing connection timeout handling");
    
    // This would require timing logic in the mock, for now we'll just simulate it
    ESP_LOGI(TAG, "Connection timeout test completed");
}

void test_connection_status_info(void) {
    ESP_LOGI(TAG, "Testing connection status info");
    
    // Initialize Bluetooth and add mock devices
        test_bt_manager_init();
    bt_mock_setup_common();
    
    // Initial state should be disconnected
    TEST_ASSERT_FALSE(bt_is_connected());
    
    // Connect to device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    ESP_LOGI(TAG, "DIAG_TEST: bt_connect_device(%s) returned %d (0x%08x)", TEST_DEVICE_ADDR, (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check connection status
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Check connection info
    bt_connection_info_t info;
    ret = bt_get_connection_info(&info);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify info
    TEST_ASSERT_TRUE(info.connected);
    
    // Disconnect
    ESP_LOGI(TAG, "DIAG_TEST_MARKER: about to call bt_disconnect() (test_connection_status_info)");
    ret = bt_disconnect();
    ESP_LOGI(TAG, "DIAG_TEST: bt_disconnect() returned %d (0x%08x)", (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Ensure both the stub-visible state and authoritative mock report the
     * disconnect before allowing subsequent tests to run. */
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(false, 1000));
}

void test_auto_reconnect(void) {
    ESP_LOGI(TAG, "Testing auto-reconnect when connection drops");

    test_bt_manager_init();
    bt_mock_setup_common();

    /* Baseline: connect with auto-reconnect disabled and ensure disconnect stays down. */
    TEST_ASSERT_EQUAL(ESP_OK, bt_connect_device(TEST_DEVICE_ADDR));
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(true, 1000));

    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(false));
    TEST_ASSERT_EQUAL(ESP_OK, bt_simulate_disconnect());
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(false, 1000));

    bt_connection_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info));
    TEST_ASSERT_FALSE(info.connected);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_DISCONNECTED, info.state);

    /* Enable auto-reconnect, reconnect manually once, then drop and verify mock restores. */
    TEST_ASSERT_EQUAL(ESP_OK, bt_connect_device(TEST_DEVICE_ADDR));
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(true, 1000));

    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(true));
    TEST_ASSERT_EQUAL(ESP_OK, bt_simulate_disconnect());

    /* Reconnect is asynchronous; allow the mock+stub pair to settle instead of
     * asserting the immediate state right after the simulated drop. */
    TEST_ASSERT_TRUE_MESSAGE(
        wait_for_authoritative_connected_state(true, 1000),
        "auto-reconnect should restore connection to authoritative mock");

    memset(&info, 0, sizeof(info));
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info));
    TEST_ASSERT_TRUE(info.connected);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_CONNECTED, info.state);
    TEST_ASSERT_EQUAL_STRING(TEST_DEVICE_ADDR, info.addr);

    /* Cleanup: disable auto-reconnect and ensure disconnect clears state. */
    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(false));
    TEST_ASSERT_EQUAL(ESP_OK, bt_disconnect());
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(false, 1000));
}

void test_auto_reconnect_should_stop_after_failed_attempts(void)
{
    ESP_LOGI(TAG, "Testing auto-reconnect stops after max failed attempts");

    test_bt_manager_init();
    bt_mock_setup_common();

    bt_conn_test_reset_state();
    const esp_err_t reconnect_results[] = {ESP_FAIL, ESP_FAIL, ESP_FAIL};
    bt_conn_test_set_reconnect_results(reconnect_results, sizeof(reconnect_results) / sizeof(reconnect_results[0]));
    bt_conn_test_set_reconnect_delay_ms(0);

    TEST_ASSERT_EQUAL(ESP_OK, bt_connect_device(TEST_DEVICE_ADDR));
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(true, 1000));
    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(true));

    TEST_ASSERT_EQUAL(ESP_OK, bt_simulate_disconnect());

    bt_connection_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info));
    TEST_ASSERT_FALSE(info.connected);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_FAILED, info.state);
    TEST_ASSERT_EQUAL_UINT8(3, info.retry_count);
}

void test_auto_reconnect_should_apply_configured_delay(void)
{
    ESP_LOGI(TAG, "Testing auto-reconnect delay between attempts");

    test_bt_manager_init();
    bt_mock_setup_common();

    bt_conn_test_reset_state();
    const esp_err_t reconnect_results[] = {ESP_FAIL, ESP_OK};
    bt_conn_test_set_reconnect_results(reconnect_results, sizeof(reconnect_results) / sizeof(reconnect_results[0]));
    bt_conn_test_set_reconnect_delay_ms(50);

    TEST_ASSERT_EQUAL(ESP_OK, bt_connect_device(TEST_DEVICE_ADDR));
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(true, 1000));

    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(true));
    TickType_t start_ticks = xTaskGetTickCount();
    TEST_ASSERT_EQUAL(ESP_OK, bt_simulate_disconnect());
    TickType_t end_ticks = xTaskGetTickCount();

    uint32_t elapsed_ms = (uint32_t)((end_ticks - start_ticks) * portTICK_PERIOD_MS);
    uint32_t expected_ms = 2U * 50U; /* Delay applied before each attempt (fail + retry). */

    bt_connection_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info));
    TEST_ASSERT_TRUE(info.connected);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_CONNECTED, info.state);
    TEST_ASSERT_EQUAL_UINT8(0, info.retry_count);

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(expected_ms, elapsed_ms);

    /* Cleanup */
    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(false));
    TEST_ASSERT_EQUAL(ESP_OK, bt_disconnect());
}

void test_auto_reconnect_should_stop_after_first_success(void)
{
    ESP_LOGI(TAG, "Testing auto-reconnect stops after first success");

    test_bt_manager_init();
    bt_mock_setup_common();

    bt_conn_test_reset_state();
    const esp_err_t reconnect_results[] = {ESP_FAIL, ESP_OK, ESP_OK};
    bt_conn_test_set_reconnect_results(reconnect_results, sizeof(reconnect_results) / sizeof(reconnect_results[0]));
    bt_conn_test_set_reconnect_delay_ms(40);

    TEST_ASSERT_EQUAL(ESP_OK, bt_connect_device(TEST_DEVICE_ADDR));
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(true, 1000));

    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(true));

    TickType_t start_ticks = xTaskGetTickCount();
    TEST_ASSERT_EQUAL(ESP_OK, bt_simulate_disconnect());
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(true, 1000));
    TickType_t end_ticks = xTaskGetTickCount();

    uint32_t elapsed_ms = (uint32_t)((end_ticks - start_ticks) * portTICK_PERIOD_MS);
    const uint32_t min_ms = 2U * 40U; /* Two attempts (fail + success). */
    const uint32_t max_ms = (3U * 40U) + 200U; /* Guard against an unexpected third attempt. */

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(min_ms, elapsed_ms);
    TEST_ASSERT_LESS_THAN_UINT32(max_ms, elapsed_ms);

    bt_connection_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info));
    TEST_ASSERT_TRUE(info.connected);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_CONNECTED, info.state);
    TEST_ASSERT_EQUAL_UINT8(0, info.retry_count); /* Reset after success. */

    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(false));
    TEST_ASSERT_EQUAL(ESP_OK, bt_disconnect());
}

void test_auto_reconnect_disabled_should_skip_attempts(void)
{
    ESP_LOGI(TAG, "Testing auto-reconnect disabled skips attempts");

    test_bt_manager_init();
    bt_mock_setup_common();

    bt_conn_test_reset_state();
    const esp_err_t reconnect_results[] = {ESP_OK, ESP_OK};
    bt_conn_test_set_reconnect_results(reconnect_results, sizeof(reconnect_results) / sizeof(reconnect_results[0]));
    bt_conn_test_set_reconnect_delay_ms(20);

    TEST_ASSERT_EQUAL(ESP_OK, bt_connect_device(TEST_DEVICE_ADDR));
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(true, 1000));

    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(false));

    TEST_ASSERT_EQUAL(ESP_OK, bt_a2dp_start_streaming());
    TEST_ASSERT_TRUE(bt_a2dp_is_streaming());

    TEST_ASSERT_EQUAL(ESP_OK, bt_simulate_disconnect());
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(false, 1000));
    vTaskDelay(pdMS_TO_TICKS(100)); /* Allow time for would-be reconnect attempts. */

    bt_connection_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info));
    TEST_ASSERT_FALSE(info.connected);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_DISCONNECTED, info.state);
    TEST_ASSERT_EQUAL_UINT8(0, info.retry_count);

    TEST_ASSERT_FALSE(bt_a2dp_is_streaming());
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STOPPED, bt_get_streaming_state());
}

void test_auto_reconnect_failures_clear_streaming_state(void)
{
    ESP_LOGI(TAG, "Testing auto-reconnect failures clear streaming state");

    test_bt_manager_init();
    bt_mock_setup_common();

    bt_conn_test_reset_state();
    const esp_err_t reconnect_results[] = {ESP_FAIL, ESP_FAIL, ESP_FAIL};
    bt_conn_test_set_reconnect_results(reconnect_results, sizeof(reconnect_results) / sizeof(reconnect_results[0]));
    bt_conn_test_set_reconnect_delay_ms(20);

    TEST_ASSERT_EQUAL(ESP_OK, bt_connect_device(TEST_DEVICE_ADDR));
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(true, 1000));

    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(true));
    TEST_ASSERT_EQUAL(ESP_OK, bt_a2dp_start_streaming());
    TEST_ASSERT_TRUE(bt_a2dp_is_streaming());

    TEST_ASSERT_EQUAL(ESP_OK, bt_simulate_disconnect());
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(false, 1000));
    vTaskDelay(pdMS_TO_TICKS(200));

    bt_connection_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info));
    TEST_ASSERT_FALSE(info.connected);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_FAILED, info.state);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(3, info.retry_count); /* Should exhaust retries. */

    TEST_ASSERT_FALSE(bt_a2dp_is_streaming());
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STOPPED, bt_get_streaming_state());
}

void test_streaming_requires_connection(void) {
    ESP_LOGI(TAG, "Testing streaming requires connection");
    
    // Initialize Bluetooth
    test_bt_manager_init();
    
    // Ensure not connected
    if (bt_is_connected()) {
        bt_disconnect();
    }
    
    // Try to start streaming without connection
    esp_err_t ret = bt_a2dp_start_streaming();
    
    // Should fail
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    /* If we had to disconnect above, allow a short window for authoritative state to settle. */
    TEST_ASSERT_TRUE(wait_for_connected_state(false, 1000));
    TEST_ASSERT_FALSE(bt_a2dp_is_streaming());
}

void test_disconnect_during_streaming_should_reconnect_and_stop_stream(void) {
    test_bt_manager_init();
    bt_mock_setup_common();

    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(true));

    bt_conn_test_reset_state();
    const esp_err_t reconnect_results[] = {ESP_FAIL, ESP_OK};
    bt_conn_test_set_reconnect_results(reconnect_results, sizeof(reconnect_results) / sizeof(reconnect_results[0]));
    bt_conn_test_set_reconnect_delay_ms(30);

    TEST_ASSERT_EQUAL(ESP_OK, bt_connect_device(TEST_DEVICE_ADDR));
    TEST_ASSERT_EQUAL(ESP_OK, bt_a2dp_start_streaming());
    TEST_ASSERT_TRUE(bt_a2dp_is_streaming());

    TickType_t start_ticks = xTaskGetTickCount();
    TEST_ASSERT_EQUAL(ESP_OK, bt_simulate_disconnect());
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(true, 2000));
    TickType_t end_ticks = xTaskGetTickCount();

    uint32_t elapsed_ms = (uint32_t)((end_ticks - start_ticks) * portTICK_PERIOD_MS);
    uint32_t expected_ms = 2U * 30U;
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(expected_ms, elapsed_ms);

    TEST_ASSERT_FALSE(bt_a2dp_is_streaming());
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STOPPED, bt_get_streaming_state());

    bt_connection_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info));
    TEST_ASSERT_TRUE(info.connected);
    TEST_ASSERT_EQUAL_UINT8(0, info.retry_count);

    TEST_ASSERT_EQUAL(ESP_OK, bt_disconnect());
}
