/* bt_a2dp_test_streaming.c — streaming scenario test bodies, split out of
 * bt_a2dp_test.c; linked into the same test_bluetooth app. */
#include "bt_a2dp_test_shared.h"

void test_connect_to_a2dp_sink(void) {
    ESP_LOGI(TAG, "Testing connecting to A2DP sink");
    
    // Initialize Bluetooth and add mock devices
    test_bt_manager_init();
    bt_mock_setup_common();
    
    // Connect to audio device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    ESP_LOGI(TAG, "DIAG_TEST: bt_connect_device(%s) returned %d (0x%08x)", TEST_DEVICE_ADDR, (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check A2DP connection status
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Check if A2DP is connected
    TEST_ASSERT_TRUE(bt_a2dp_is_connected());
    
    // Disconnect
    ESP_LOGI(TAG, "DIAG_TEST_MARKER: about to call bt_disconnect() (test_connect_to_a2dp_sink)");
    ret = bt_disconnect();
    ESP_LOGI(TAG, "DIAG_TEST: bt_disconnect() returned %d (0x%08x)", (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

void test_a2dp_streaming(void) {
    ESP_LOGI(TAG, "Testing A2DP streaming start/stop");
    
    // Initialize Bluetooth and add mock devices
    test_bt_manager_init();
    bt_mock_setup_common();
    
    // Connect to audio device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    ESP_LOGI(TAG, "DIAG_TEST: bt_connect_device(%s) returned %d (0x%08x)", TEST_DEVICE_ADDR, (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Start audio streaming
    ret = bt_a2dp_start_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check if streaming
    TEST_ASSERT_TRUE(bt_a2dp_is_streaming());
    
    // Stop streaming
    ret = bt_a2dp_stop_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check if not streaming
    TEST_ASSERT_FALSE(bt_a2dp_is_streaming());
    
    // Disconnect
    ESP_LOGI(TAG, "DIAG_TEST_MARKER: about to call bt_disconnect() (test_a2dp_streaming)");
    ret = bt_disconnect();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

void test_a2dp_paired_devices(void) {
    ESP_LOGI(TAG, "Testing A2DP paired devices memory");
    
    // Initialize Bluetooth
    test_bt_manager_init();
    bt_mock_setup_paired_devices(); // Use our paired device setup (renamed from bt_test_setup_paired_devices)
    
    // Check if device is paired
    TEST_ASSERT_TRUE(bt_is_device_paired(TEST_DEVICE_ADDR));
}

void test_audio_streaming_start_success(void) {
    ESP_LOGI(TAG, "Testing audio streaming start success");
    
    // Initialize Bluetooth and add mock devices
    test_bt_manager_init();
    bt_mock_setup_common();
    
    // Connect to audio device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Start audio streaming
    ret = bt_a2dp_start_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify streaming state
    TEST_ASSERT_TRUE(bt_a2dp_is_streaming());
    
    // Stop streaming and disconnect
    bt_a2dp_stop_streaming();
    bt_disconnect();
}

void test_audio_streaming_stop_success(void) {
    ESP_LOGI(TAG, "Testing audio streaming stop success");
    
    // Initialize Bluetooth and add mock devices
    test_bt_manager_init();
    bt_mock_setup_common();
    
    // Connect to audio device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Start audio streaming
    ret = bt_a2dp_start_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Stop streaming
    ret = bt_a2dp_stop_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify not streaming
    TEST_ASSERT_FALSE(bt_a2dp_is_streaming());
    
    // Disconnect
    bt_disconnect();
}

void test_streaming_pause_resume(void) {
    ESP_LOGI(TAG, "Testing streaming pause and resume");
    
    // Initialize Bluetooth and add mock devices
    test_bt_manager_init();
    bt_mock_setup_common();
    
    // Connect to audio device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Start streaming
    ret = bt_a2dp_start_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(bt_a2dp_is_streaming());
    
    // Pause streaming
    ret = bt_a2dp_pause_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(bt_a2dp_is_streaming());
    
    // Resume streaming
    ret = bt_a2dp_resume_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(bt_a2dp_is_streaming());
    
    // Stop streaming and disconnect
    bt_a2dp_stop_streaming();
    bt_disconnect();
}

void test_streaming_state_reporting(void) {
    ESP_LOGI(TAG, "Testing streaming state reporting");
    
    // Initialize Bluetooth and add mock devices
    test_bt_manager_init();
    bt_mock_setup_common();
    
    // Initial state should be not streaming
    TEST_ASSERT_FALSE(bt_a2dp_is_streaming());
    
    // Connect to audio device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Start streaming
    ret = bt_a2dp_start_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(bt_a2dp_is_streaming());
    
    // Stop streaming
    ret = bt_a2dp_stop_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(bt_a2dp_is_streaming());
    
    // Disconnect
    bt_disconnect();
}

void test_remote_suspend_and_resume_should_toggle_stream_state(void) {
    test_bt_manager_init();
    bt_mock_setup_common();
    TEST_ASSERT_EQUAL(ESP_OK, bt_connect_device(TEST_DEVICE_ADDR));

    esp_a2d_cb_param_t param = {0};
    parse_test_addr(param.audio_stat.remote_bda);

    param.audio_stat.state = ESP_A2D_AUDIO_STATE_STARTED;
    bt_manager_test_invoke_a2dp_event(ESP_A2D_AUDIO_STATE_EVT, &param);
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STREAMING, bt_get_streaming_state());

    param.audio_stat.state = ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND;
    bt_manager_test_invoke_a2dp_event(ESP_A2D_AUDIO_STATE_EVT, &param);
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_PAUSED, bt_get_streaming_state());

    param.audio_stat.state = ESP_A2D_AUDIO_STATE_STARTED;
    bt_manager_test_invoke_a2dp_event(ESP_A2D_AUDIO_STATE_EVT, &param);
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STREAMING, bt_get_streaming_state());

    TEST_ASSERT_EQUAL(ESP_OK, bt_disconnect());
}
