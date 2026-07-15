/**
 * test_uart_audio_cmd.c — UARTAUDIO command plumbing + cmd_process gate
 *
 * Covers:
 *   - cmd_parse recognizes UARTAUDIO with subcommand/baud params
 *   - START: OK|UARTAUDIO|STARTING response (default + explicit baud),
 *     streaming flag set; response is queued BEFORE the flag flips
 *   - START rejections: bad baud, already streaming (BUSY)
 *   - STATUS: reports streaming flag + uart_source state
 *   - STOP in text mode: ERR NOT_STREAMING (real stop is the in-band frame)
 *   - missing/unknown subcommand: ERR BAD_ARGS
 *   - cmd_process gate: while streaming, the command loop must not touch
 *     UART RX; pending bytes are processed only after streaming ends
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "cmd_handlers.h"
#include "command_interface.h"
#include "uart_audio.h"
#include "mock_uart.h"

extern void cmd_test_reset_cmd_process_state(void);

void setUp(void)
{
    mock_uart_init(115200);
    cmd_init();
    cmd_test_reset_cmd_process_state();
    uart_audio_test_reset();
    mock_uart_reset_tx();
}

void tearDown(void)
{
    uart_audio_test_reset();
    cmd_deinit();
}

static cmd_status_t run_command(const char *line)
{
    cmd_context_t ctx;
    cmd_status_t st = cmd_parse(line, &ctx);
    if (st != CMD_SUCCESS) {
        return st;
    }
    return cmd_execute(&ctx);
}

/* ── parsing ────────────────────────────────────────────────────────────── */

void test_parse_uartaudio_with_subcommand_and_baud(void)
{
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("UARTAUDIO START 460800", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_UARTAUDIO, ctx.type);
    TEST_ASSERT_EQUAL_INT(2, ctx.param_count);
    TEST_ASSERT_EQUAL_STRING("START", ctx.params[0]);
    TEST_ASSERT_EQUAL_STRING("460800", ctx.params[1]);
}

/* ── START ──────────────────────────────────────────────────────────────── */

void test_start_default_baud_sets_streaming(void)
{
    TEST_ASSERT_FALSE(uart_audio_is_streaming());

    run_command("UARTAUDIO START");

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|UARTAUDIO|STARTING"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "baud=921600"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "frame=1024"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "ring="));
    TEST_ASSERT_NOT_NULL(strstr(tx, "a2dp="));
    TEST_ASSERT_TRUE(uart_audio_is_streaming());
}

void test_start_explicit_baud_echoed(void)
{
    run_command("UARTAUDIO START 921600");

    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "baud=921600"));
    TEST_ASSERT_TRUE(uart_audio_is_streaming());
}

void test_start_invalid_baud_rejected(void)
{
    run_command("UARTAUDIO START 12345");

    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "ERR|UARTAUDIO|BAD_BAUD"));
    TEST_ASSERT_FALSE(uart_audio_is_streaming());
}

void test_start_at_115200_rejected(void)
{
    /* 115200 baud cannot carry 88.2 KB/s of PCM — must be rejected */
    run_command("UARTAUDIO START 115200");

    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "ERR|UARTAUDIO|BAD_BAUD"));
    TEST_ASSERT_FALSE(uart_audio_is_streaming());
}

void test_start_while_streaming_is_busy(void)
{
    run_command("UARTAUDIO START");
    TEST_ASSERT_TRUE(uart_audio_is_streaming());
    mock_uart_reset_tx();

    /* direct handler call: cmd_process would normally be gated already */
    run_command("UARTAUDIO START");

    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "ERR|UARTAUDIO|BUSY"));
}

/* ── STATUS / STOP / bad args ───────────────────────────────────────────── */

void test_status_reports_idle_state(void)
{
    run_command("UARTAUDIO STATUS");

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|UARTAUDIO|STATUS"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "streaming=0"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "state=INACTIVE"));
}

void test_stop_in_text_mode_not_streaming(void)
{
    run_command("UARTAUDIO STOP");

    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "ERR|UARTAUDIO|NOT_STREAMING"));
}

void test_missing_subcommand_bad_args(void)
{
    run_command("UARTAUDIO");

    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "ERR|UARTAUDIO|BAD_ARGS"));
}

void test_unknown_subcommand_bad_args(void)
{
    run_command("UARTAUDIO FROB");

    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "ERR|UARTAUDIO|BAD_ARGS"));
}

/* ── cmd_process streaming gate ─────────────────────────────────────────── */

void test_cmd_process_gated_while_streaming(void)
{
    /* enter streaming mode */
    run_command("UARTAUDIO START");
    TEST_ASSERT_TRUE(uart_audio_is_streaming());
    mock_uart_reset_tx();

    /* bytes arrive on the UART while the reader task owns it */
    mock_uart_inject_rx_data("STATUS\r\n", 8);
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());

    /* gate must prevent any read/response: TX silent, RX untouched */
    TEST_ASSERT_EQUAL_STRING("", mock_uart_get_tx_data());
    TEST_ASSERT_EQUAL_size_t(8, mock_uart_get_available_bytes(1));

    /* streaming ends (reader task exit path clears the flag last) */
    uart_audio_test_reset();
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());

    /* the queued STATUS command is now processed normally */
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "|STATUS|"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_uartaudio_with_subcommand_and_baud);
    RUN_TEST(test_start_default_baud_sets_streaming);
    RUN_TEST(test_start_explicit_baud_echoed);
    RUN_TEST(test_start_invalid_baud_rejected);
    RUN_TEST(test_start_at_115200_rejected);
    RUN_TEST(test_start_while_streaming_is_busy);
    RUN_TEST(test_status_reports_idle_state);
    RUN_TEST(test_stop_in_text_mode_not_streaming);
    RUN_TEST(test_missing_subcommand_bad_args);
    RUN_TEST(test_unknown_subcommand_bad_args);
    RUN_TEST(test_cmd_process_gated_while_streaming);
    return UNITY_END();
}
