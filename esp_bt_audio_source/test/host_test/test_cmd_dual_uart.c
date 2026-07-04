/**
 * test_cmd_dual_uart.c — dual-UART command interface
 *
 * The command processor listens on the primary (console/USB) UART and an
 * optional secondary UART simultaneously. Contract under test:
 *   - commands on the PRIMARY port keep working exactly as before, with
 *     the response on the primary port only (USB behavior is preserved)
 *   - commands on the SECONDARY port respond on the secondary port only
 *   - async EVENT| lines broadcast to BOTH ports
 *   - per-port line buffers: a partial line on one port never mixes with
 *     traffic on the other
 *   - while UARTAUDIO streaming owns the primary port, the secondary
 *     port still processes commands (primary RX untouched)
 *
 * Host mapping: primary = mock UART_NUM_1 (legacy), secondary = UART_NUM_0.
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "cmd_handlers.h"
#include "command_interface.h"
#include "uart_audio.h"
#include "mock_uart.h"

#define PRIMARY   1  /* CMD_UART_NUM on host */
#define SECONDARY 0  /* CMD_UART_SECONDARY on host */

extern void cmd_test_reset_cmd_process_state(void);

void setUp(void)
{
    mock_uart_init_port(PRIMARY, 115200);
    mock_uart_init_port(SECONDARY, 115200);
    cmd_init();
    cmd_test_reset_cmd_process_state();
    uart_audio_test_reset();
    mock_uart_reset_tx_port(PRIMARY);
    mock_uart_reset_tx_port(SECONDARY);
}

void tearDown(void)
{
    uart_audio_test_reset();
    cmd_deinit();
}

/* ── primary (USB) behavior preserved ───────────────────────────────────── */

void test_primary_port_commands_still_work(void)
{
    mock_uart_inject_rx_data_port(PRIMARY, "VERSION\r\n", 9);
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());

    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data_port(PRIMARY), "OK|VERSION|"));
    /* and nothing leaked onto the secondary port */
    TEST_ASSERT_NULL(strstr(mock_uart_get_tx_data_port(SECONDARY), "OK|VERSION|"));
}

/* ── secondary port ─────────────────────────────────────────────────────── */

void test_secondary_port_command_responds_on_secondary_only(void)
{
    mock_uart_inject_rx_data_port(SECONDARY, "VERSION\r\n", 9);
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());

    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data_port(SECONDARY), "OK|VERSION|"));
    TEST_ASSERT_NULL(strstr(mock_uart_get_tx_data_port(PRIMARY), "OK|VERSION|"));
}

void test_both_ports_processed_in_one_cycle(void)
{
    mock_uart_inject_rx_data_port(PRIMARY, "VERSION\r\n", 9);
    mock_uart_inject_rx_data_port(SECONDARY, "HELP\r\n", 6);
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());

    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data_port(PRIMARY), "OK|VERSION|"));
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data_port(SECONDARY), "HELP"));
    TEST_ASSERT_NULL(strstr(mock_uart_get_tx_data_port(PRIMARY), "HELP|"));
    TEST_ASSERT_NULL(strstr(mock_uart_get_tx_data_port(SECONDARY), "OK|VERSION|"));
}

/* ── events broadcast ───────────────────────────────────────────────────── */

void test_events_broadcast_to_both_ports(void)
{
    cmd_send_event_pair("SUCCESS", "11:22:33:44:55:66");

    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data_port(PRIMARY),
                                "EVENT|PAIR|SUCCESS|11:22:33:44:55:66"));
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data_port(SECONDARY),
                                "EVENT|PAIR|SUCCESS|11:22:33:44:55:66"));
}

/* ── per-port line buffering ────────────────────────────────────────────── */

void test_partial_lines_do_not_mix_between_ports(void)
{
    /* half a command on primary, full command on secondary */
    mock_uart_inject_rx_data_port(PRIMARY, "VER", 3);
    mock_uart_inject_rx_data_port(SECONDARY, "HELP\r\n", 6);
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());

    /* secondary completed; primary still buffering — no UNKNOWN error */
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data_port(SECONDARY), "HELP"));
    TEST_ASSERT_EQUAL_STRING("", mock_uart_get_tx_data_port(PRIMARY));

    /* finish the primary command: halves must join into VERSION */
    mock_uart_inject_rx_data_port(PRIMARY, "SION\r\n", 6);
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data_port(PRIMARY), "OK|VERSION|"));
}

/* ── UARTAUDIO streaming gate ───────────────────────────────────────────── */

void test_streaming_gates_primary_but_not_secondary(void)
{
    /* enter streaming mode (reader task owns the PRIMARY port) */
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("UARTAUDIO START", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    TEST_ASSERT_TRUE(uart_audio_is_streaming());
    mock_uart_reset_tx_port(PRIMARY);
    mock_uart_reset_tx_port(SECONDARY);

    mock_uart_inject_rx_data_port(PRIMARY, "STATUS\r\n", 8);
    mock_uart_inject_rx_data_port(SECONDARY, "VERSION\r\n", 9);
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());

    /* primary untouched (reader task owns those bytes) */
    TEST_ASSERT_EQUAL_size_t(8, mock_uart_get_available_bytes(PRIMARY));
    /* secondary keeps working mid-stream */
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data_port(SECONDARY), "OK|VERSION|"));

    /* stream ends: the queued primary command processes normally */
    uart_audio_test_reset();
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data_port(PRIMARY), "|STATUS|"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_primary_port_commands_still_work);
    RUN_TEST(test_secondary_port_command_responds_on_secondary_only);
    RUN_TEST(test_both_ports_processed_in_one_cycle);
    RUN_TEST(test_events_broadcast_to_both_ports);
    RUN_TEST(test_partial_lines_do_not_mix_between_ports);
    RUN_TEST(test_streaming_gates_primary_but_not_secondary);
    return UNITY_END();
}
