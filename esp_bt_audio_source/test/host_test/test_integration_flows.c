#include <string.h>

#include "unity.h"
#include "command_interface.h"
#include "mock_uart.h"
#include "bt_manager.h"
#include "audio_processor.h"
#include "nvs_storage.h"

extern void nvs_storage_mock_reset(void);
extern void bt_manager_test_reset_forces(void);
extern void bt_manager_test_set_force_start_failure(int v);
extern int bt_manager_test_get_scan_start_count(void);
extern int bt_manager_test_get_start_audio_calls(void);
extern void bt_manager_test_reset_btstate_mock(void);
extern void mock_gap_set_remove_bond_result(esp_err_t result);
extern void mock_gap_set_bond_device_count(int count);
extern int bt_manager_test_get_unpair_all_temp_alloc_outstanding(void);
extern int bt_manager_test_get_unpair_all_temp_alloc_peak(void);
extern void nvs_storage_mock_set_get_count_result(esp_err_t err);
extern void nvs_storage_mock_set_get_device_result(esp_err_t err);
extern void audio_processor_test_idle_i2s_failures(int failures, bool synth_enabled, size_t beep_remaining, bool *synth_after, int *failures_after);

static int s_pair_start_called = 0;
static char s_pair_start_mac[32] = {0};

void bt_manager_test_record_pair_start(const char* mac)
{
    s_pair_start_called++;
    if (mac != NULL) {
        strncpy(s_pair_start_mac, mac, sizeof(s_pair_start_mac) - 1);
        s_pair_start_mac[sizeof(s_pair_start_mac) - 1] = '\0';
    } else {
        s_pair_start_mac[0] = '\0';
    }
}

static void run_cmd(const char* cmd)
{
    cmd_context_t ctx = {0};
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse(cmd, &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
}

void setUp(void)
{
    mock_uart_init(115200);
    cmd_init();
    cmd_test_reset_cmd_process_state();

    bt_manager_init_t cfg = {
        .device_name = "IntegrationHost",
        .connected_cb = NULL,
        .disconnected_cb = NULL,
    };
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_init(&cfg));

    nvs_storage_mock_reset();
    bt_manager_test_reset_forces();
    bt_manager_test_reset_btstate_mock();
    nvs_storage_clear_paired_devices();

    s_pair_start_called = 0;
    s_pair_start_mac[0] = '\0';
    mock_uart_reset_tx();
}

void tearDown(void)
{
    cmd_deinit();
    bt_manager_deinit();
}

void test_scan_connect_pair_start_flow_should_bridge_command_bt_and_audio_layers(void)
{
    const char* mac = "AA:BB:CC:DD:EE:01";

    run_cmd("SCAN");
    TEST_ASSERT_EQUAL_INT(1, bt_manager_test_get_scan_start_count());
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "OK|SCAN|MOCK_STARTED"));

    mock_uart_reset_tx();
    run_cmd("CONNECT AA:BB:CC:DD:EE:01");
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "OK|CONNECT|MOCK_CONNECTED"));

    bt_manager_mock_connection_established(mac, "SinkOne");
    TEST_ASSERT_EQUAL_INT(1, bt_manager_is_connected());

    mock_uart_reset_tx();
    run_cmd("PAIR AA:BB:CC:DD:EE:01");
    TEST_ASSERT_EQUAL_INT(1, s_pair_start_called);
    TEST_ASSERT_EQUAL_STRING(mac, s_pair_start_mac);
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "OK|PAIR|MOCK_INITIATED"));

    bt_manager_mock_pairing_complete(mac, true);
    int paired_count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&paired_count));
    TEST_ASSERT_EQUAL_INT(1, paired_count);

    mock_uart_reset_tx();
    run_cmd("START");
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "OK|START|MOCK_STARTED"));
    TEST_ASSERT_TRUE(audio_processor_is_running());
}

void test_beep_flow_should_preserve_running_stream_after_beep_drain(void)
{
    uint8_t scratch[512] = {0};
    size_t bytes_read = 0;

    bt_manager_mock_connection_established("AA:BB:CC:DD:EE:02", "SinkTwo");
    mock_uart_reset_tx();
    run_cmd("START");
    TEST_ASSERT_TRUE(audio_processor_is_running());

    mock_uart_reset_tx();
    run_cmd("BEEP");
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "OK|BEEP|SENT"));
    TEST_ASSERT_TRUE(audio_processor_is_beep_active());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(scratch, sizeof(scratch), &bytes_read));
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)bytes_read);
    TEST_ASSERT_FALSE(audio_processor_is_beep_active());
    TEST_ASSERT_TRUE(audio_processor_is_running());
}

void test_pairing_persistence_flow_should_reflect_in_paired_and_unpair_commands(void)
{
    const char* mac = "AA:BB:CC:DD:EE:03";

    bt_manager_mock_pairing_complete(mac, true);

    mock_uart_reset_tx();
    run_cmd("PAIRED");
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "OK|PAIRED|COUNT|1"));

    mock_uart_reset_tx();
    run_cmd("UNPAIR AA:BB:CC:DD:EE:03");
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "OK|UNPAIR|REMOVED"));

    int paired_count = -1;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&paired_count));
    TEST_ASSERT_EQUAL_INT(0, paired_count);
}

void test_start_should_report_error_when_bt_layer_start_fails(void)
{
    bt_manager_test_set_force_start_failure(1);
    bt_manager_mock_connection_established("AA:BB:CC:DD:EE:04", "SinkFour");

    mock_uart_reset_tx();
    run_cmd("START");

    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "ERR|START|FAILED"));
}

void test_unpair_all_error_recovery_should_not_leak_temp_bond_allocations(void)
{
    const int attempts = 64;

    mock_gap_set_bond_device_count(8);
    mock_gap_set_remove_bond_result(ESP_FAIL);

    for (int i = 0; i < attempts; ++i) {
        mock_uart_reset_tx();
        run_cmd("UNPAIR_ALL");
        TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "ERR|UNPAIR_ALL|FAILED"));
        TEST_ASSERT_EQUAL_INT(0, bt_manager_test_get_unpair_all_temp_alloc_outstanding());
    }

    TEST_ASSERT_GREATER_THAN_INT(0, bt_manager_test_get_unpair_all_temp_alloc_peak());
}

void test_bt_disconnect_reconnect_should_resume_streaming_when_start_retried(void)
{
    bt_manager_mock_connection_established("AA:BB:CC:DD:EE:12", "RecoverSink");

    mock_uart_reset_tx();
    run_cmd("START");
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "OK|START|MOCK_STARTED"));
    TEST_ASSERT_TRUE(audio_processor_is_running());

    bt_manager_mock_connection_closed("AA:BB:CC:DD:EE:12");

    mock_uart_reset_tx();
    run_cmd("START");
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "ERR|START|FAILED"));

    bt_manager_mock_connection_established("AA:BB:CC:DD:EE:12", "RecoverSink");

    mock_uart_reset_tx();
    run_cmd("START");
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "OK|START|MOCK_STARTED"));
    TEST_ASSERT_TRUE(audio_processor_is_running());
}

void test_i2s_failure_should_fallback_to_synth_and_recover_back_to_i2s_mode(void)
{
    bool synth_after = false;
    int failures_after = -1;

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());
    audio_processor_set_synth_mode(false);
    TEST_ASSERT_FALSE(audio_processor_is_synth_mode_enabled());

    audio_processor_test_idle_i2s_failures(25, false, 0, &synth_after, &failures_after);
    TEST_ASSERT_TRUE(synth_after);
    TEST_ASSERT_EQUAL_INT(0, failures_after);

    audio_processor_set_synth_mode(synth_after);
    TEST_ASSERT_TRUE(audio_processor_is_synth_mode_enabled());

    audio_processor_set_synth_mode(false);
    TEST_ASSERT_FALSE(audio_processor_is_synth_mode_enabled());
}

void test_nvs_corruption_should_recover_with_repair_and_persist_pairing(void)
{
    const char* mac = "AA:BB:CC:DD:EE:13";

    nvs_storage_mock_set_get_count_result(ESP_FAIL);
    nvs_storage_mock_set_get_device_result(ESP_FAIL);

    mock_uart_reset_tx();
    run_cmd("PAIRED");
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "ERR|PAIRED|READ_FAILED"));

    nvs_storage_mock_set_get_count_result(ESP_OK);
    nvs_storage_mock_set_get_device_result(ESP_OK);

    bt_manager_mock_pairing_complete(mac, true);

    mock_uart_reset_tx();
    run_cmd("PAIRED");
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "OK|PAIRED|COUNT|1"));
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "INFO|PAIRED|ITEM|"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_scan_connect_pair_start_flow_should_bridge_command_bt_and_audio_layers);
    RUN_TEST(test_beep_flow_should_preserve_running_stream_after_beep_drain);
    RUN_TEST(test_pairing_persistence_flow_should_reflect_in_paired_and_unpair_commands);
    RUN_TEST(test_start_should_report_error_when_bt_layer_start_fails);
    RUN_TEST(test_unpair_all_error_recovery_should_not_leak_temp_bond_allocations);
    RUN_TEST(test_bt_disconnect_reconnect_should_resume_streaming_when_start_retried);
    RUN_TEST(test_i2s_failure_should_fallback_to_synth_and_recover_back_to_i2s_mode);
    RUN_TEST(test_nvs_corruption_should_recover_with_repair_and_persist_pairing);
    return UNITY_END();
}
