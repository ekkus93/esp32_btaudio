/**
 * test_cmd_handlers_system.c — unit tests for cmd_handlers_system.c
 *
 * Covers:
 *   - cmd_handle_status()  happy path, streaming-info-failure fallback,
 *                          zero-callbacks underrun rate safety
 *   - cmd_handle_help()    basic output check
 *   - cmd_handle_version() non-empty version string
 *   - cmd_handle_spanlog() host-side behaviour (ESP_PLATFORM guard)
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "cmd_handlers.h"
#include "command_interface.h"
#include "mock_uart.h"

/* Provided by mocks/mock_audio_and_btstate.c */
void bt_manager_test_force_streaming_info_failure(bool force);
void bt_manager_test_reset_btstate_mock(void);
void bt_manager_test_set_connection_info(bool connected, const char *mac);

/* Test-controlled VERSION override. cmd_handle_version() (host build) calls the weak
 * cmd_version_host_override(): returning NULL exercises the HOST-MOCK default branch,
 * a non-empty string exercises the override branch. */
static const char *g_ver_override = NULL;
const char *cmd_version_host_override(void)
{
    return g_ver_override;
}

void setUp(void)
{
    mock_uart_init(115200);
    cmd_init();
    mock_uart_reset_tx();
    bt_manager_test_reset_btstate_mock();
    g_ver_override = NULL;
}

void tearDown(void)
{
    bt_manager_test_force_streaming_info_failure(false);
    cmd_deinit();
}

/* ── cmd_handle_status — happy path ────────────────────────────────────── */

void test_status_happy_path_sends_ok_response(void)
{
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_STATUS;

    mock_uart_reset_tx();
    cmd_handle_status(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|STATUS|CURRENT|"));
}

void test_status_contains_key_fields(void)
{
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_STATUS;

    mock_uart_reset_tx();
    cmd_handle_status(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    /* Must have MUTE, SAMPLE_RATE, PAIRED_COUNT in the data */
    TEST_ASSERT_NOT_NULL(strstr(tx, "MUTE="));
    TEST_ASSERT_NOT_NULL(strstr(tx, "SAMPLE_RATE="));
    TEST_ASSERT_NOT_NULL(strstr(tx, "PAIRED_COUNT="));
}

/* ── cmd_handle_status — CONN_MAC (connected-sink MAC for the S3/web UI) ─── */

void test_status_includes_conn_mac_field(void)
{
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_STATUS;

    /* Not connected: the field is still present (empty value). */
    bt_manager_test_set_connection_info(false, NULL);
    mock_uart_reset_tx();
    cmd_handle_status(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "CONN_MAC="));
}

void test_status_reports_connected_mac(void)
{
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_STATUS;

    bt_manager_test_set_connection_info(true, "48:78:5E:D9:35:A3");
    mock_uart_reset_tx();
    cmd_handle_status(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "CONN_MAC=48:78:5E:D9:35:A3"));
}

/* ── cmd_handle_status — streaming info failure fallback ──────────────── */

void test_status_streaming_info_failure_sends_unavailable(void)
{
    bt_manager_test_force_streaming_info_failure(true);

    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_STATUS;

    mock_uart_reset_tx();
    cmd_handle_status(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    /* Still sends OK|STATUS even when streaming info is unavailable */
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|STATUS|CURRENT|"));
    /* Falls back to the UNAVAILABLE data field */
    TEST_ASSERT_NOT_NULL(strstr(tx, "STREAM_INFO=UNAVAILABLE"));
}

void test_status_streaming_info_failure_still_includes_basic_fields(void)
{
    bt_manager_test_force_streaming_info_failure(true);

    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_STATUS;

    mock_uart_reset_tx();
    cmd_handle_status(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "MUTE="));
    TEST_ASSERT_NOT_NULL(strstr(tx, "PAIRED_COUNT="));
    TEST_ASSERT_NOT_NULL(strstr(tx, "INIT="));
}

/* ── cmd_handle_status — zero total_callbacks (underrun rate safety) ───── */

void test_status_zero_callbacks_underrun_rate_is_zero(void)
{
    /* bt_get_streaming_info mock returns total_callbacks=0 by default.
     * The handler must not divide by zero; underrun rate should be 0.00. */
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_STATUS;

    mock_uart_reset_tx();
    cmd_handle_status(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    /* Response must appear (not crash) and contain UNDERRUN_RATE=0.00 */
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|STATUS|CURRENT|"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "UNDERRUN_RATE=0.00"));
}

/* ── cmd_handle_help ────────────────────────────────────────────────────── */

void test_help_sends_ok_response(void)
{
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_HELP;

    mock_uart_reset_tx();
    cmd_handle_help(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|HELP|"));
}

void test_help_output_is_not_empty(void)
{
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_HELP;

    mock_uart_reset_tx();
    cmd_handle_help(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    /* Help output should list at least some commands — check for SCAN entry */
    TEST_ASSERT_NOT_NULL(strstr(tx, "SCAN"));
}

/* ── cmd_handle_version ─────────────────────────────────────────────────── */

void test_version_sends_ok_response(void)
{
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_VERSION;

    mock_uart_reset_tx();
    cmd_handle_version(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|VERSION|"));
}

void test_version_string_is_non_empty(void)
{
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_VERSION;

    mock_uart_reset_tx();
    cmd_handle_version(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);

    /* Response is "OK|VERSION|<version>\r\n".
     * Skip past the second pipe to find the version string. */
    const char *version_start = strstr(tx, "OK|VERSION|");
    TEST_ASSERT_NOT_NULL(version_start);
    version_start += strlen("OK|VERSION|");
    /* The version string must be non-empty (not immediately \r or \n or NUL) */
    TEST_ASSERT_TRUE(*version_start != '\0' && *version_start != '\r' && *version_start != '\n');
}

/* ── cmd_handle_spanlog — host path (no ESP_PLATFORM) ──────────────────── */

void test_spanlog_host_returns_without_crash(void)
{
    /* On host, the SPANLOG handler has an #ifdef ESP_PLATFORM guard.
     * Verify it doesn't crash and sends some response. */
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_SPANLOG;
    /* No params — use default count */

    mock_uart_reset_tx();
    cmd_status_t result = cmd_handle_spanlog(&ctx);

    /* Must return without crashing; return value should be CMD_SUCCESS
     * or CMD_ERROR_INVALID_PARAM but not something undefined. */
    TEST_ASSERT_TRUE(result == CMD_SUCCESS || result == CMD_ERROR_INVALID_PARAM);
}

/* ── cmd_handle_version — override branch ──────────────────────────────── */

void test_version_override_used_when_present(void)
{
    g_ver_override = "9.9.9-test";
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_VERSION;

    mock_uart_reset_tx();
    cmd_handle_version(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|VERSION|9.9.9-test"));
}

void test_version_defaults_to_host_mock(void)
{
    g_ver_override = NULL; /* no override → HOST-MOCK default branch */
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = CMD_TYPE_VERSION;

    mock_uart_reset_tx();
    cmd_handle_version(&ctx);

    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|VERSION|HOST-MOCK"));
}

/* ── cmd_handle_mem / audio_status / reset / parts (host #else branches) ─── */

void test_mem_reports_mock_stats(void)
{
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    mock_uart_reset_tx();
    cmd_handle_mem(&ctx);
    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|MEM|MOCK"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "DRAM=0"));
}

void test_audio_status_reports_mock(void)
{
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    mock_uart_reset_tx();
    cmd_handle_audio_status(&ctx);
    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|AUDIO_STATUS|MOCK"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "SOURCE=MOCK"));
}

void test_reset_sends_mock_reboot(void)
{
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    mock_uart_reset_tx();
    cmd_status_t r = cmd_handle_reset(&ctx);
    TEST_ASSERT_EQUAL_INT(CMD_SUCCESS, r); /* host: does not actually restart */
    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|RESET|MOCK_REBOOT"));
}

void test_parts_unsupported_on_host(void)
{
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    mock_uart_reset_tx();
    cmd_handle_parts(&ctx);
    const char *tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|PARTS|UNSUPPORTED"));
}

/* ── I2S diagnostic handlers (host #else MOCK branches) ────────────────── */

void test_i2s_probe_rxtest_clkgen_mock(void)
{
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    mock_uart_reset_tx();
    cmd_handle_i2s_probe(&ctx);
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "OK|I2SPROBE|MOCK"));

    mock_uart_reset_tx();
    cmd_handle_i2s_rxtest(&ctx);
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "OK|I2SRXTEST|MOCK"));

    mock_uart_reset_tx();
    cmd_handle_i2s_clkgen(&ctx);
    TEST_ASSERT_NOT_NULL(strstr(mock_uart_get_tx_data(), "OK|I2SCLKGEN|MOCK"));
}

int main(void)
{
    UNITY_BEGIN();

    /* cmd_handle_status */
    RUN_TEST(test_status_happy_path_sends_ok_response);
    RUN_TEST(test_status_contains_key_fields);
    RUN_TEST(test_status_streaming_info_failure_sends_unavailable);
    RUN_TEST(test_status_streaming_info_failure_still_includes_basic_fields);
    RUN_TEST(test_status_zero_callbacks_underrun_rate_is_zero);
    RUN_TEST(test_status_includes_conn_mac_field);
    RUN_TEST(test_status_reports_connected_mac);

    /* cmd_handle_help */
    RUN_TEST(test_help_sends_ok_response);
    RUN_TEST(test_help_output_is_not_empty);

    /* cmd_handle_version */
    RUN_TEST(test_version_sends_ok_response);
    RUN_TEST(test_version_string_is_non_empty);
    RUN_TEST(test_version_override_used_when_present);
    RUN_TEST(test_version_defaults_to_host_mock);

    /* cmd_handle_mem / audio_status / reset / parts */
    RUN_TEST(test_mem_reports_mock_stats);
    RUN_TEST(test_audio_status_reports_mock);
    RUN_TEST(test_reset_sends_mock_reboot);
    RUN_TEST(test_parts_unsupported_on_host);

    /* I2S diagnostics */
    RUN_TEST(test_i2s_probe_rxtest_clkgen_mock);

    /* cmd_handle_spanlog */
    RUN_TEST(test_spanlog_host_returns_without_crash);

    return UNITY_END();
}
