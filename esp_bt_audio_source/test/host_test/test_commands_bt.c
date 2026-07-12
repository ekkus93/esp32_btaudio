/* test_commands_bt.c — bt-group test bodies, split out of
 * test_commands.c; linked into the same test_commands executable. */
#include "test_commands_shared.h"

/* --- test_scan_invokes_manager --- */
void test_scan_invokes_manager(void) {
    mock_uart_reset_tx();

    // Ensure forces/hooks reset
    bt_manager_test_reset_forces();

    // Initial count should be zero
    int before = bt_manager_test_get_scan_start_count();

    // Inject command into mock UART and process
    mock_uart_inject_rx_data("SCAN\r\n", 6);
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());

    // Manager test hook should have been called at least once
    int after = bt_manager_test_get_scan_start_count();
    TEST_ASSERT_TRUE(after > before);

    // Also validate we emitted a response mentioning SCAN
    const char* tx_data = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx_data);
    TEST_ASSERT_TRUE(strstr(tx_data, "SCAN") != NULL);
}

/* --- test_confirm_pin_command --- */
void test_confirm_pin_command(void) {
    mock_gap_reset();
    // Execute confirm command (MAC + accept)
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("CONFIRM_PIN AA:BB:CC:DD:EE:FF ACCEPT", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* mac = mock_gap_get_last_mac();
    TEST_ASSERT_NOT_NULL(mac);
    // Mock formats hex lowercase
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", mac);
    TEST_ASSERT_EQUAL(1, mock_gap_get_last_confirm());
}

/* --- test_enter_pin_command --- */
void test_enter_pin_command(void) {
    mock_gap_reset();
    // Execute enter pin command
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("ENTER_PIN AA:BB:CC:DD:EE:FF 1234", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* mac = mock_gap_get_last_mac();
    TEST_ASSERT_NOT_NULL(mac);
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", mac);
    TEST_ASSERT_EQUAL(4, mock_gap_get_last_pin_len());
    TEST_ASSERT_EQUAL_STRING("1234", mock_gap_get_last_pin());
}

/* --- test_unpair_command_success --- */
void test_unpair_command_success(void) {
    bt_manager_test_reset_forces();
    nvs_storage_clear_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("AA:BB:CC:DD:EE:FF", "Speaker"));

    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("UNPAIR AA:BB:CC:DD:EE:FF", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|UNPAIR|REMOVED|AA:BB:CC:DD:EE:FF"));

    const char* last = bt_manager_test_get_last_unpair_mac();
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", last);

    int count = -1;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL(0, count);
}

/* --- test_unpair_command_failure --- */
void test_unpair_command_failure(void) {
    bt_manager_test_reset_forces();
    nvs_storage_clear_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("AA:BB:CC:DD:EE:FF", "Speaker"));

    bt_manager_test_set_force_unpair_failure(1);
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("UNPAIR AA:BB:CC:DD:EE:FF", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|UNPAIR|FAILED|AA:BB:CC:DD:EE:FF"));

    const char* last = bt_manager_test_get_last_unpair_mac();
    TEST_ASSERT_NOT_NULL(last);
        TEST_ASSERT_EQUAL('\0', last[0]);

    int count = -1;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL(1, count);

    bt_manager_test_reset_forces();
}

/* --- test_unpair_command_not_found --- */
void test_unpair_command_not_found(void) {
    bt_manager_test_reset_forces();
    nvs_storage_clear_paired_devices();

    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("UNPAIR AA:BB:CC:DD:EE:FF", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|UNPAIR|NOT_FOUND|AA:BB:CC:DD:EE:FF"));

    const char* last = bt_manager_test_get_last_unpair_mac();
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", last);

    int count = -1;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL(0, count);
}

/* --- test_unpair_all_command --- */
void test_unpair_all_command(void) {
    nvs_storage_clear_paired_devices();
    // Seed mock NVS with paired devices
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("aa:bb:cc:11:22:33", "Speaker"));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("11:22:33:44:55:66", "Phone"));

    int before = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&before));
    TEST_ASSERT_TRUE(before >= 2);

    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("UNPAIR_ALL", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|UNPAIR_ALL|SUCCESS|2"));

    TEST_ASSERT_EQUAL(2, bt_manager_test_get_unpair_all_cleared_before());
    TEST_ASSERT_EQUAL(0, bt_manager_test_get_unpair_all_removed());

    int after = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&after));
    TEST_ASSERT_EQUAL(0, after);
}

/* --- test_unpair_all_command_failure --- */
void test_unpair_all_command_failure(void) {
    nvs_storage_clear_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("AA:BB:CC:DD:EE:01", "One"));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("AA:BB:CC:DD:EE:02", "Two"));

    bt_manager_test_set_force_unpair_all_failure(1);

    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("UNPAIR_ALL", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|UNPAIR_ALL|FAILED"));

    int remaining = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&remaining));
    TEST_ASSERT_EQUAL(2, remaining);

    bt_manager_test_set_force_unpair_all_failure(0);
}

/* --- test_paired_command --- */
void test_paired_command(void) {
    // Seed mock NVS
    nvs_storage_clear_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("aa:bb:cc:11:22:33", "Speaker"));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("11:22:33:44:55:66", "Phone"));

    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("PAIRED", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "PAIRED") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "COUNT") != NULL);
}

/* --- test_disconnect_command --- */
void test_disconnect_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("DISCONNECT", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "DISCONNECT") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "MOCK_DONE") != NULL || strstr(tx, "DONE") != NULL || strstr(tx, "FAILED") != NULL);
}

/* --- test_disconnect_failure_command --- */
void test_disconnect_failure_command(void) {
    mock_uart_reset_tx();
    // Enable forced failure
    extern void bt_manager_test_set_force_disconnect_failure(int v);
    extern void bt_manager_test_reset_forces(void);
    bt_manager_test_set_force_disconnect_failure(1);

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("DISCONNECT", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "DISCONNECT") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "ERR") != NULL || strstr(tx, "FAILED") != NULL);

    // Reset forces for other tests
    bt_manager_test_reset_forces();
}

/* --- test_beep_command_not_connected --- */
void test_beep_command_not_connected(void) {
    mock_uart_reset_tx();
    // Ensure mock BT has no connection
    // Some harnesses default to disconnected; be explicit by closing
    extern void bt_manager_mock_connection_closed(const char* mac);
    bt_manager_mock_connection_closed("aa:bb:cc:11:22:33");

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("BEEP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "BEEP") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "NOT_CONNECTED") != NULL || strstr(tx, "ERR") != NULL);
}

/* --- test_beep_command_connected --- */
void test_beep_command_connected(void) {
    mock_uart_reset_tx();
    // Simulate a BT connection
    extern void bt_manager_mock_connection_established(const char* mac, const char* name);
    bt_manager_mock_connection_established("aa:bb:cc:11:22:33", "MockSpeaker");

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("BEEP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "BEEP") != NULL);
    // Accept either SENT/OK or a mock variant depending on harness
    TEST_ASSERT_TRUE(strstr(tx, "SENT") != NULL || strstr(tx, "OK") != NULL || strstr(tx, "MOCK") != NULL);

    // Verify that beep was actually triggered
    TEST_ASSERT_TRUE(audio_processor_is_beep_active());

    // Verify the BEEP command requested a 10s middle-C tone
    uint32_t dur_ms = 0; double freq_hz = 0.0;
    audio_processor_get_last_beep_request(&dur_ms, &freq_hz);
    TEST_ASSERT_EQUAL_UINT32(10000U, dur_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 261.63f, (float)freq_hz);
}

