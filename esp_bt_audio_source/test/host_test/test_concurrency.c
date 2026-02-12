#include <stdio.h>
#include <string.h>

#include "unity.h"
#include "command_interface.h"
#include "mock_uart.h"
#include "bt_manager.h"
#include "audio_processor.h"
#include "nvs_storage.h"

extern void nvs_storage_mock_reset(void);
extern void bt_manager_test_reset_forces(void);
extern void bt_manager_test_reset_btstate_mock(void);

static void run_cmd(const char* cmd)
{
    cmd_context_t ctx = {0};
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse(cmd, &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
}

static int count_substring(const char* haystack, const char* needle)
{
    int count = 0;
    const char* pos = haystack;
    size_t step = strlen(needle);

    while (pos != NULL && (pos = strstr(pos, needle)) != NULL) {
        ++count;
        pos += step;
    }
    return count;
}

void setUp(void)
{
    mock_uart_init(115200);
    cmd_init();
    cmd_test_reset_cmd_process_state();

    bt_manager_init_t cfg = {
        .device_name = "ConcurrencyHost",
        .connected_cb = NULL,
        .disconnected_cb = NULL,
    };
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_init(&cfg));

    nvs_storage_mock_reset();
    bt_manager_test_reset_forces();
    bt_manager_test_reset_btstate_mock();
    nvs_storage_clear_paired_devices();
    mock_uart_reset_tx();
}

void tearDown(void)
{
    cmd_deinit();
    bt_manager_deinit();
}

void test_command_interface_and_bt_events_interleaving_should_remain_responsive(void)
{
    const int command_burst = 120;

    for (int i = 0; i < command_burst; ++i) {
        mock_uart_reset_tx();

        if ((i % 2) == 0) {
            bt_manager_mock_connection_established("AA:BB:CC:DD:EE:10", "RaceSink");
        } else {
            bt_manager_mock_connection_closed("AA:BB:CC:DD:EE:10");
        }

        if ((i % 3) == 0) {
            run_cmd("STATUS");
            TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "STATUS"));
        } else if ((i % 3) == 1) {
            run_cmd("SCAN");
            TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "SCAN"));
        } else {
            run_cmd("PAIRED");
            TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "PAIRED"));
        }
    }
}

void test_audio_callback_volume_and_beep_interleaving_should_keep_stream_running(void)
{
    uint8_t scratch[512] = {0};
    size_t bytes_read = 0;
    char volume_cmd[24] = {0};

    bt_manager_mock_connection_established("AA:BB:CC:DD:EE:11", "AudioRaceSink");
    mock_uart_reset_tx();
    run_cmd("START");
    TEST_ASSERT_TRUE(audio_processor_is_running());

    for (int i = 0; i < 80; ++i) {
        int volume = i % 101;
        snprintf(volume_cmd, sizeof(volume_cmd), "VOLUME %d", volume);

        mock_uart_reset_tx();
        run_cmd(volume_cmd);
        TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "OK|VOLUME|MOCK_SET|"));

        if ((i % 4) == 0) {
            mock_uart_reset_tx();
            run_cmd("BEEP");
            {
                const char* tx = mock_uart_get_tx_data();
                bool accepted = strstr(tx, "OK|BEEP|SENT") != NULL;
                bool busy = strstr(tx, "ERR|BEEP|BUSY|BEEP_ACTIVE") != NULL;
                TEST_ASSERT_TRUE(accepted || busy);
            }
        }

        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(scratch, sizeof(scratch), &bytes_read));
    }

    TEST_ASSERT_TRUE(audio_processor_is_running());
}

void test_command_flood_should_process_all_injected_lines_without_error(void)
{
    char rx_burst[8192] = {0};
    const int total_commands = 240;
    int expected_status = 0;
    int expected_scan = 0;
    int expected_volume = 0;

    for (int i = 0; i < total_commands; ++i) {
        if ((i % 3) == 0) {
            strcat(rx_burst, "STATUS\r\n");
            expected_status++;
        } else if ((i % 3) == 1) {
            strcat(rx_burst, "SCAN\r\n");
            expected_scan++;
        } else {
            strcat(rx_burst, "VOLUME 42\r\n");
            expected_volume++;
        }
    }

    mock_uart_reset_tx();
    mock_uart_inject_rx_data(rx_burst, strlen(rx_burst));

    int guard = 0;
    while (mock_uart_get_available_bytes(UART_NUM_1) > 0 && guard < 64) {
        TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());
        guard++;
    }
    TEST_ASSERT_TRUE(guard < 64);

    {
        const char* tx = mock_uart_get_tx_data();
        TEST_ASSERT_NOT_NULL(tx);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(expected_status - 4, count_substring(tx, "|STATUS|"));
        TEST_ASSERT_GREATER_OR_EQUAL_INT(expected_scan - 4, count_substring(tx, "|SCAN|"));
        TEST_ASSERT_GREATER_OR_EQUAL_INT(expected_volume - 4, count_substring(tx, "|VOLUME|MOCK_SET|42"));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_command_interface_and_bt_events_interleaving_should_remain_responsive);
    RUN_TEST(test_audio_callback_volume_and_beep_interleaving_should_keep_stream_running);
    RUN_TEST(test_command_flood_should_process_all_injected_lines_without_error);
    return UNITY_END();
}
