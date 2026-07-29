/* Exact late-terminal contract exercised inside the established Bluetooth
 * integration runner. DISCONNECTED is authoritative; a later STOPPED callback
 * has no trusted active binding and must remain an observable no-op. */
void test_bt_a2dp_disconnect_then_late_stop_is_suppressed(void)
{
    bt_manager_set_autostart_enabled(true);

    esp_a2d_cb_param_t param = {0};
    const uint8_t bda[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    const esp_a2d_conn_hdl_t conn_handle = 19U;
    memcpy(param.conn_stat.remote_bda, bda, sizeof(bda));
    param.conn_stat.conn_hdl = conn_handle;
    param.conn_stat.state = ESP_A2D_CONNECTION_STATE_CONNECTED;

    bt_manager_test_invoke_a2dp_event(ESP_A2D_CONNECTION_STATE_EVT, &param);
    TEST_ASSERT_EQUAL(1, bt_manager_is_connected());

    param.conn_stat.state = ESP_A2D_CONNECTION_STATE_DISCONNECTED;
    bt_manager_test_invoke_a2dp_event(ESP_A2D_CONNECTION_STATE_EVT, &param);
    TEST_ASSERT_FALSE(bt_manager_is_connected());
    TEST_ASSERT_FALSE(bt_manager_test_is_audio_playing());
    TEST_ASSERT_EQUAL(ESP_A2D_CONNECTION_STATE_DISCONNECTED,
                      bt_manager_test_get_last_conn_state());

    bt_events_a2dp_binding_snapshot_t before = {0};
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&before));
    TEST_ASSERT_FALSE(before.valid);
    const int audio_callback_before = bt_manager_test_get_last_audio_state();

    memset(&param, 0, sizeof(param));
    param.audio_stat.state = ESP_A2D_AUDIO_STATE_STOPPED;
    param.audio_stat.conn_hdl = conn_handle;
    memcpy(param.audio_stat.remote_bda, bda, sizeof(bda));
    bt_manager_test_invoke_a2dp_event(ESP_A2D_AUDIO_STATE_EVT, &param);

    bt_events_a2dp_binding_snapshot_t after = {0};
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_binding(&after));
    TEST_ASSERT_FALSE(bt_manager_test_is_audio_playing());
    TEST_ASSERT_EQUAL(audio_callback_before,
                      bt_manager_test_get_last_audio_state());
    TEST_ASSERT_EQUAL_UINT64(before.late_terminal_events_ignored + 1U,
                             after.late_terminal_events_ignored);
    TEST_ASSERT_EQUAL_UINT64(before.missing_binding_rejections,
                             after.missing_binding_rejections);
    TEST_ASSERT_EQUAL_UINT64(before.wrong_peer_rejections,
                             after.wrong_peer_rejections);
    TEST_ASSERT_EQUAL_UINT64(before.stale_handle_rejections,
                             after.stale_handle_rejections);
    TEST_ASSERT_EQUAL_UINT64(before.generation_sync_failures,
                             after.generation_sync_failures);
    TEST_ASSERT_EQUAL(0, bt_manager_test_get_start_audio_calls());
}
