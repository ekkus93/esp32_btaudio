#include "commands_priv.h"
#include "cmd_handlers.h"
#include "uart_audio.h"

#define TAG "CMD_IF"

__attribute__((weak)) void cmd_test_capture_response(const char *line);

static uint32_t s_event_sequence = 0;

#ifndef CMD_BUF_SIZE
#define CMD_BUF_SIZE 256
#endif

/* Command ports polled by cmd_process(). Index 0 is always the primary
 * (console/USB) UART; the optional secondary UART is purely additive. */
static const int s_cmd_ports[] = {
    CMD_UART_NUM,
#ifdef CMD_UART_SECONDARY
    CMD_UART_SECONDARY,
#endif
};
#define CMD_PORT_COUNT (sizeof(s_cmd_ports) / sizeof(s_cmd_ports[0]))

/* per-port line accumulators — bytes from different ports never mix */
static char s_cmd_line_buf[CMD_PORT_COUNT][CMD_BUF_SIZE];
static size_t s_cmd_line_len[CMD_PORT_COUNT];

/* Port the currently-executing command arrived on; responses go here.
 * Async callers (events) never depend on it — events broadcast. */
static int s_reply_uart = CMD_UART_NUM;

#if defined(UNIT_TEST)
void cmd_test_reset_cmd_process_state(void)
{
    memset(s_cmd_line_buf, 0, sizeof(s_cmd_line_buf));
    memset(s_cmd_line_len, 0, sizeof(s_cmd_line_len));
    s_reply_uart = CMD_UART_NUM;
}
#endif

#if defined(UNIT_TEST) || defined(ESP_PLATFORM)
void cmd_reset_event_sequence(void)
{
    s_event_sequence = 0;
}
#endif

cmd_status_t cmd_init(void)
{
#if defined(UNIT_TEST) && !defined(ESP_PLATFORM)
    audio_processor_deinit();
    audio_processor_init(NULL);
    extern void bt_manager_test_reset_btstate_mock(void);
    bt_manager_test_reset_btstate_mock();
#endif
#if defined(ESP_PLATFORM) && !defined(UNIT_TEST) && defined(CMD_UART_SECONDARY)
    /* Secondary command UART (Kconfig). Configured here — not main.c —
     * because uart_param_config/uart_set_pin belong to the cmd layer
     * (tools/ci_check_main_layering.sh enforces this for main.c). */
    const uart_config_t uart2_cfg = {
        .baud_rate = CONFIG_CMD_UART2_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t u2err = uart_param_config(CMD_UART_SECONDARY, &uart2_cfg);
    if (u2err == ESP_OK) {
        u2err = uart_set_pin(CMD_UART_SECONDARY, CONFIG_CMD_UART2_TX_PIN,
                             CONFIG_CMD_UART2_RX_PIN,
                             UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (u2err == ESP_OK) {
        u2err = uart_driver_install(CMD_UART_SECONDARY, 1024, 1024, 0, NULL, 0);
    }
    if (u2err != ESP_OK) {
        /* degrade gracefully: primary/USB command port is unaffected */
        ESP_LOGW(TAG, "secondary command UART init failed: %s", esp_err_to_name(u2err));
    } else {
        ESP_LOGI(TAG, "secondary command UART ready: uart=%d tx=%d rx=%d baud=%d",
                 CMD_UART_SECONDARY, CONFIG_CMD_UART2_TX_PIN,
                 CONFIG_CMD_UART2_RX_PIN, CONFIG_CMD_UART2_BAUD);
    }
#endif
    return CMD_SUCCESS;
}

cmd_status_t cmd_deinit(void)
{
    return CMD_SUCCESS;
}

static void cmd_write_port(int port, const char *buf, size_t len)
{
#if defined(UNIT_TEST) || !defined(ESP_PLATFORM)
    uart_write_bytes(port, buf, len);
#else
    if (uart_is_driver_installed(port))
    {
        uart_write_bytes(port, buf, len);
    }
#endif
}

cmd_status_t cmd_send_response(const char *status, const char *command, const char *result, const char *data)
{
    char buf[512];
    const char *d = data ? data : "";
    int len = snprintf(buf, sizeof(buf), "%s|%s|%s|%s\r\n", status ? status : "", command ? command : "", result ? result : "", d);

    /* EVENT lines are asynchronous notifications — broadcast to every
     * command port. Direct responses go only to the port the command
     * came from (USB behavior unchanged when the command came via USB). */
    if (status != NULL && strcmp(status, "EVENT") == 0)
    {
        for (size_t i = 0; i < CMD_PORT_COUNT; i++)
        {
            cmd_write_port(s_cmd_ports[i], buf, (size_t)len);
        }
    }
    else
    {
        cmd_write_port(s_reply_uart, buf, (size_t)len);
    }
    if (cmd_test_capture_response)
    {
        cmd_test_capture_response(buf);
    }
    return CMD_SUCCESS;
}

cmd_status_t cmd_send_event_pair(const char *subtype, const char *data)
{
    uint32_t seq = ++s_event_sequence;
    uint64_t ts_ms = cmd_get_timestamp_ms();

    const char *d = data ? data : "";
    char payload[256];
    if (d[0] != '\0')
    {
        snprintf(payload, sizeof(payload), "%s,SEQ=%lu,TS=%llu", d, (unsigned long)seq, (unsigned long long)ts_ms);
    }
    else
    {
        snprintf(payload, sizeof(payload), "SEQ=%lu,TS=%llu", (unsigned long)seq, (unsigned long long)ts_ms);
    }

    char buf[512];
    int chars_written = snprintf(buf, sizeof(buf), "EVENT|PAIR|%s|%s", subtype ? subtype : "", payload);
    cmd_send_response("EVENT", "PAIR", subtype ? subtype : "", payload);

#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "DIAG-EVENT: %s", buf);  // NOLINT(bugprone-branch-clone)
#else
    printf("DIAG-EVENT: %s\n", buf);
#endif

#if defined(__GNUC__)
    extern void test_push_event(const char *event) __attribute__((weak));
#else
    extern void test_push_event(const char *event);
#endif
    if (chars_written > 0 && test_push_event)
    {
        printf("HOOK-DEBUG: test_push_event symbol present, forwarding event\n");
        test_push_event(buf);
    }
    else
    {
        printf("HOOK-DEBUG: test_push_event not present or n<=0\n");
    }

    return CMD_SUCCESS;
}

cmd_status_t cmd_parse(const char *cmd_str, cmd_context_t *ctx)
{
    if (!cmd_str || !ctx) {
        return CMD_ERROR_INVALID_PARAM;
    }

    memset(ctx, 0, sizeof(*ctx));

    char buf[512];
    cmd_safe_copy(buf, sizeof(buf), cmd_str);
    char *s = buf;
    while (*s && isspace((unsigned char)*s)) {
        ++s;
    }
    /* Guard: if the entire string was whitespace, strlen(s) == 0 and
     * computing s - 1 would be UB.  Bail out early. */
    if (*s == '\0') {
        return CMD_ERROR_UNKNOWN;
    }
    char *end = s + strlen(s) - 1;
    while (end >= s && isspace((unsigned char)*end))
    {
        *end = '\0';
        --end;
    }
    if (*s == '\0') {
        return CMD_ERROR_UNKNOWN;
    }

    char *save = NULL;
    char *token = strtok_r(s, " \t", &save);
    if (!token) {
        return CMD_ERROR_UNKNOWN;
    }

    if (strcasecmp(token, "SCAN") == 0) {
        ctx->type = CMD_TYPE_SCAN;
    }
    else if (strcasecmp(token, "AUDIO_STATUS") == 0) {
        ctx->type = CMD_TYPE_AUDIO_STATUS;
    }
    else if (strcasecmp(token, "CONNECT") == 0) {
        ctx->type = CMD_TYPE_CONNECT;
    }
    else if (strcasecmp(token, "CONNECT_NAME") == 0) {
        ctx->type = CMD_TYPE_CONNECT_NAME;
    }
    else if (strcasecmp(token, "DISCONNECT") == 0) {
        ctx->type = CMD_TYPE_DISCONNECT;
    }
    else if (strcasecmp(token, "PAIRED") == 0) {
        ctx->type = CMD_TYPE_PAIRED;
    }
    else if (strcasecmp(token, "SET_NAME") == 0) {
        ctx->type = CMD_TYPE_SET_NAME;
    }
    else if (strcasecmp(token, "START") == 0) {
        ctx->type = CMD_TYPE_START;
    }
    else if (strcasecmp(token, "STOP") == 0) {
        ctx->type = CMD_TYPE_STOP;
    }
    else if (strcasecmp(token, "VOLUME") == 0) {
        ctx->type = CMD_TYPE_VOLUME;
    }
    else if (strcasecmp(token, "MUTE") == 0) {
        ctx->type = CMD_TYPE_MUTE;
    }
    else if (strcasecmp(token, "UNMUTE") == 0) {
        ctx->type = CMD_TYPE_UNMUTE;
    }
    else if (strcasecmp(token, "STATUS") == 0) {
        ctx->type = CMD_TYPE_STATUS;
    }
    else if (strcasecmp(token, "VERSION") == 0) {
        ctx->type = CMD_TYPE_VERSION;
    }
    else if (strcasecmp(token, "RESET") == 0) {
        ctx->type = CMD_TYPE_RESET;
    }
    else if (strcasecmp(token, "DEBUG") == 0) {
        ctx->type = CMD_TYPE_DEBUG;
    }
    else if (strcasecmp(token, "SAMPLE_RATE") == 0) {
        ctx->type = CMD_TYPE_SAMPLE_RATE;
    }
    else if (strcasecmp(token, "MEM") == 0) {
        ctx->type = CMD_TYPE_MEM;
    }
    else if (strcasecmp(token, "SYNTH") == 0) {
        ctx->type = CMD_TYPE_SYNTH;
    }
    else if (strcasecmp(token, "I2S_CONFIG") == 0) {
        ctx->type = CMD_TYPE_I2S_CONFIG;
    }
    else if (strcasecmp(token, "BEEP") == 0) {
        ctx->type = CMD_TYPE_BEEP;
    }
    else if (strcasecmp(token, "PAIR") == 0) {
        ctx->type = CMD_TYPE_PAIR;
    }
    else if (strcasecmp(token, "CONFIRM_PIN") == 0) {
        ctx->type = CMD_TYPE_CONFIRM_PIN;
    }
    else if (strcasecmp(token, "ENTER_PIN") == 0) {
        ctx->type = CMD_TYPE_ENTER_PIN;
    }
    else if (strcasecmp(token, "SET_DEFAULT_PIN") == 0) {
        ctx->type = CMD_TYPE_SET_DEFAULT_PIN;
    }
    else if (strcasecmp(token, "UNPAIR") == 0) {
        ctx->type = CMD_TYPE_UNPAIR;
    }
    else if (strcasecmp(token, "UNPAIR_ALL") == 0) {
        ctx->type = CMD_TYPE_UNPAIR_ALL;
    }
    else if (strcasecmp(token, "FILE") == 0) {
        ctx->type = CMD_TYPE_FILE;
    }
    else if (strcasecmp(token, "FILES") == 0) {
        ctx->type = CMD_TYPE_FILES;
    }
    else if (strcasecmp(token, "PARTS") == 0) {
        ctx->type = CMD_TYPE_PARTS;
    }
    else if (strcasecmp(token, "AUDIO_AUTOSTART") == 0) {
        ctx->type = CMD_TYPE_AUDIO_AUTOSTART;
    }
    else if (strcasecmp(token, "LAST_MAC") == 0) {
        ctx->type = CMD_TYPE_LAST_MAC;
    }
    else if (strcasecmp(token, "DIAG") == 0) {
        ctx->type = CMD_TYPE_DIAG;
    }
    else if (strcasecmp(token, "UARTAUDIO") == 0) {
        ctx->type = CMD_TYPE_UARTAUDIO;
    }
    else if (strcasecmp(token, "SPANLOG") == 0) {
        ctx->type = CMD_TYPE_SPANLOG;
    }
    else if (strcasecmp(token, "HELP") == 0) {
        ctx->type = CMD_TYPE_HELP;
    }
    else {
        ctx->type = CMD_TYPE_UNKNOWN;
    }

    if (ctx->type == CMD_TYPE_CONNECT_NAME)
    {
        char *rest = save;
        while (rest && *rest && isspace((unsigned char)*rest)) {
            ++rest;
        }
        if (rest && *rest)
        {
            cmd_safe_copy(ctx->params[0], CMD_MAX_PARAM_LEN, rest);
            ctx->param_count = 1;
        }
        else
        {
            ctx->param_count = 0;
        }
        return CMD_SUCCESS;
    }

    int idx = 0;
    while ((token = strtok_r(NULL, " \t", &save)) != NULL && idx < CMD_MAX_PARAMS)
    {
        cmd_safe_copy(ctx->params[idx], CMD_MAX_PARAM_LEN, token);
        ++idx;
    }
    ctx->param_count = idx;

    return (ctx->type == CMD_TYPE_UNKNOWN) ? CMD_ERROR_UNKNOWN : CMD_SUCCESS;
}

/* Read pending bytes from one port and execute any complete lines.
 * pi indexes s_cmd_ports and the per-port line accumulators. */
static void cmd_process_port(size_t pi)
{
    const int read_uart = s_cmd_ports[pi];
    char *line_buf = s_cmd_line_buf[pi];
    uint8_t read_buf[CMD_BUF_SIZE];

#if !defined(UNIT_TEST) && defined(ESP_PLATFORM)
    if (!uart_is_driver_installed(read_uart))
    {
        return;  /* port not ready (e.g. secondary UART disabled) */
    }
#endif

    int bytes_read = uart_read_bytes(read_uart, read_buf, sizeof(read_buf) - 1, 0);
    if (bytes_read <= 0)
    {
        return;
    }

    size_t to_copy = (size_t)bytes_read;
    if (s_cmd_line_len[pi] + to_copy >= CMD_BUF_SIZE)
    {
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG, "cmd_process: line buffer overflow on uart %d, resetting", read_uart);  // NOLINT(bugprone-branch-clone)
#endif
        s_cmd_line_len[pi] = 0;
        to_copy = CMD_BUF_SIZE - 1;
    }
    memcpy(line_buf + s_cmd_line_len[pi], read_buf, to_copy);
    s_cmd_line_len[pi] += to_copy;
    line_buf[s_cmd_line_len[pi]] = '\0';

    /* responses for lines found below belong to this port */
    s_reply_uart = read_uart;

    char *start = line_buf;
    while (true)
    {
        char *newline_pos = (char *)memchr(start, '\n', (size_t)(line_buf + s_cmd_line_len[pi] - start));
        char *cr_pos = (char *)memchr(start, '\r', (size_t)(line_buf + s_cmd_line_len[pi] - start));
        char *term = newline_pos ? newline_pos : cr_pos;
        if (!term) {
            break;
        }

        *term = '\0';
        char *line_end = term - 1;
        while (line_end >= start && isspace((unsigned char)*line_end))
        {
            *line_end = '\0';
            --line_end;
        }

        cmd_context_t ctx;
        cmd_status_t parse_status = cmd_parse(start, &ctx);
        if (parse_status == CMD_SUCCESS)
        {
            cmd_execute(&ctx);
        }
        else if (parse_status == CMD_ERROR_UNKNOWN)
        {
            cmd_send_response("ERR", "UNKNOWN", "COMMAND_NOT_FOUND", NULL);
        }

        start = term + 1;
        while (start < line_buf + s_cmd_line_len[pi] && (*start == '\n' || *start == '\r')) {
            ++start;
        }
    }

    size_t remaining = (size_t)(line_buf + s_cmd_line_len[pi] - start);
    if (remaining > 0) {
        memmove(line_buf, start, remaining);
    }
    s_cmd_line_len[pi] = remaining;
    line_buf[s_cmd_line_len[pi]] = '\0';

    s_reply_uart = CMD_UART_NUM;
}

cmd_status_t cmd_process(void)
{
    for (size_t pi = 0; pi < CMD_PORT_COUNT; pi++)
    {
        /* UARTAUDIO gate: while streaming, the reader task owns the
         * PRIMARY UART's RX — never consume a byte from it here. The
         * secondary port keeps serving commands mid-stream. */
        if (s_cmd_ports[pi] == CMD_UART_NUM && uart_audio_is_streaming())
        {
            continue;
        }
        cmd_process_port(pi);
    }
    return CMD_SUCCESS;
}

cmd_status_t cmd_execute(const cmd_context_t *ctx)
{
    if (ctx == NULL) {
        return CMD_ERROR_NOT_INITIALIZED;
    }

    switch (ctx->type)
    {
    case CMD_TYPE_STATUS:
        return cmd_handle_status(ctx);
    case CMD_TYPE_AUDIO_STATUS:
        return cmd_handle_audio_status(ctx);
    case CMD_TYPE_MEM:
        return cmd_handle_mem(ctx);
    case CMD_TYPE_VERSION:
        return cmd_handle_version(ctx);
    case CMD_TYPE_RESET:
        return cmd_handle_reset(ctx);
    case CMD_TYPE_SCAN:
        return cmd_handle_scan(ctx);
    case CMD_TYPE_SYNTH:
        return cmd_handle_synth(ctx);
    case CMD_TYPE_DIAG:
        return cmd_handle_diag(ctx);
    case CMD_TYPE_BEEP:
        return cmd_handle_beep(ctx);
    case CMD_TYPE_CONNECT:
        return cmd_handle_connect(ctx);
    case CMD_TYPE_DISCONNECT:
        return cmd_handle_disconnect(ctx);
    case CMD_TYPE_START:
        return cmd_handle_start(ctx);
    case CMD_TYPE_STOP:
        return cmd_handle_stop(ctx);
    case CMD_TYPE_FILE:
        return cmd_handle_file(ctx);
    case CMD_TYPE_FILES:
        return cmd_handle_files(ctx);
    case CMD_TYPE_PARTS:
        return cmd_handle_parts(ctx);
    case CMD_TYPE_CONNECT_NAME:
        return cmd_handle_connect_name(ctx);
    case CMD_TYPE_CONFIRM_PIN:
        return cmd_handle_confirm_pin(ctx);
    case CMD_TYPE_ENTER_PIN:
        return cmd_handle_enter_pin(ctx);
    case CMD_TYPE_MUTE:
        return cmd_handle_mute(ctx);
    case CMD_TYPE_UNMUTE:
        return cmd_handle_unmute(ctx);
    case CMD_TYPE_VOLUME:
        return cmd_handle_volume(ctx);
    case CMD_TYPE_I2S_CONFIG:
        return cmd_handle_i2s_config(ctx);
    case CMD_TYPE_AUDIO_AUTOSTART:
        return cmd_handle_audio_autostart(ctx);
    case CMD_TYPE_LAST_MAC:
        return cmd_handle_last_mac(ctx);
    case CMD_TYPE_PAIR:
        return cmd_handle_pair(ctx);
    case CMD_TYPE_PAIRED:
        return cmd_handle_paired(ctx);
    case CMD_TYPE_SAMPLE_RATE:
        return cmd_handle_sample_rate(ctx);
    case CMD_TYPE_SET_NAME:
        return cmd_handle_set_name(ctx);
    case CMD_TYPE_DEBUG:
        return cmd_handle_debug(ctx);
    case CMD_TYPE_SET_DEFAULT_PIN:
        return cmd_handle_set_default_pin(ctx);
    case CMD_TYPE_UNPAIR:
        return cmd_handle_unpair(ctx);
    case CMD_TYPE_UNPAIR_ALL:
        return cmd_handle_unpair_all(ctx);
    case CMD_TYPE_SPANLOG:
        return cmd_handle_spanlog(ctx);
    case CMD_TYPE_UARTAUDIO:
        return cmd_handle_uartaudio(ctx);
    case CMD_TYPE_HELP:
        return cmd_handle_help(ctx);
    default:
        cmd_send_response("INFO", "COMMAND", "RECEIVED", "Not implemented yet");
        return CMD_SUCCESS;
    }
}
