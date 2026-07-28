#include "commands_priv.h"
#include "cmd_handlers.h"
#include "uart_audio.h"

#define TAG "CMD_IF"
#define CMD_RESPONSE_BUF_SIZE 512U

__attribute__((weak)) void cmd_test_capture_response(const char *line);

static uint32_t s_event_sequence;

#ifndef CMD_BUF_SIZE
#define CMD_BUF_SIZE 256
#endif

static const int s_cmd_ports[] = {
    CMD_UART_NUM,
#ifdef CMD_UART_SECONDARY
    CMD_UART_SECONDARY,
#endif
};
#define CMD_PORT_COUNT (sizeof(s_cmd_ports) / sizeof(s_cmd_ports[0]))

static char s_cmd_line_buf[CMD_PORT_COUNT][CMD_BUF_SIZE];
static size_t s_cmd_line_len[CMD_PORT_COUNT];
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
    s_event_sequence = 0U;
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
        u2err = uart_driver_install(CMD_UART_SECONDARY, 1024, 1024,
                                    0, NULL, 0);
    }
    if (u2err != ESP_OK) {
        ESP_LOGW(TAG, "secondary command UART init failed: %s",
                 esp_err_to_name(u2err));
    } else {
        ESP_LOGI(TAG,
                 "secondary command UART ready: uart=%d tx=%d rx=%d baud=%d",
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
    if (uart_is_driver_installed(port)) {
        uart_write_bytes(port, buf, len);
    }
#endif
}

static cmd_status_t format_response(char *buf, size_t buf_size,
                                    const char *status,
                                    const char *command,
                                    const char *result,
                                    const char *data,
                                    size_t *length_out)
{
    if (buf == NULL || buf_size == 0U || length_out == NULL) {
        return CMD_ERROR_INVALID_PARAM;
    }

    const char *safe_status = status != NULL ? status : "";
    const char *safe_command = command != NULL ? command : "";
    const char *safe_result = result != NULL ? result : "";
    const char *safe_data = data != NULL ? data : "";
    int written = snprintf(buf, buf_size, "%s|%s|%s|%s\r\n",
                           safe_status, safe_command, safe_result, safe_data);
    if (written < 0) return CMD_ERROR_UNKNOWN;
    if ((size_t)written >= buf_size) {
        written = snprintf(buf, buf_size,
                           "ERR|%s|RESPONSE_TOO_LONG|\r\n", safe_command);
        if (written < 0 || (size_t)written >= buf_size) {
            return CMD_ERROR_TOO_MANY_PARAMS;
        }
        *length_out = (size_t)written;
        return CMD_ERROR_TOO_MANY_PARAMS;
    }

    *length_out = (size_t)written;
    return CMD_SUCCESS;
}

static void capture_response(const char *buf)
{
    if (cmd_test_capture_response != NULL) cmd_test_capture_response(buf);
}

cmd_status_t cmd_send_response(const char *status, const char *command,
                               const char *result, const char *data)
{
    char buf[CMD_RESPONSE_BUF_SIZE];
    size_t len = 0U;
    cmd_status_t format_status = format_response(
        buf, sizeof(buf), status, command, result, data, &len);
    if (format_status != CMD_SUCCESS &&
        format_status != CMD_ERROR_TOO_MANY_PARAMS) {
        return format_status;
    }

    if (status != NULL && strcmp(status, CMD_STATUS_EVENT) == 0) {
        for (size_t index = 0U; index < CMD_PORT_COUNT; ++index) {
            cmd_write_port(s_cmd_ports[index], buf, len);
        }
    } else {
        cmd_write_port(s_reply_uart, buf, len);
    }
    capture_response(buf);
    return format_status;
}

cmd_status_t cmd_send_response_all(const char *status, const char *command,
                                   const char *result, const char *data)
{
    char buf[CMD_RESPONSE_BUF_SIZE];
    size_t len = 0U;
    cmd_status_t format_status = format_response(
        buf, sizeof(buf), status, command, result, data, &len);
    if (format_status != CMD_SUCCESS &&
        format_status != CMD_ERROR_TOO_MANY_PARAMS) {
        return format_status;
    }

    for (size_t index = 0U; index < CMD_PORT_COUNT; ++index) {
        cmd_write_port(s_cmd_ports[index], buf, len);
    }
    capture_response(buf);
    return format_status;
}

cmd_status_t cmd_send_event_pair(const char *subtype, const char *data)
{
    uint32_t seq = ++s_event_sequence;
    uint64_t ts_ms = cmd_get_timestamp_ms();
    const char *safe_data = data != NULL ? data : "";
    char payload[256];
    if (safe_data[0] != '\0') {
        snprintf(payload, sizeof(payload), "%s,SEQ=%lu,TS=%llu",
                 safe_data, (unsigned long)seq,
                 (unsigned long long)ts_ms);
    } else {
        snprintf(payload, sizeof(payload), "SEQ=%lu,TS=%llu",
                 (unsigned long)seq, (unsigned long long)ts_ms);
    }

    char diagnostic[512];
    int chars_written = snprintf(diagnostic, sizeof(diagnostic),
                                 "EVENT|PAIR|%s|%s",
                                 subtype != NULL ? subtype : "", payload);
    (void)cmd_send_response(CMD_STATUS_EVENT, "PAIR",
                            subtype != NULL ? subtype : "", payload);

#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "DIAG-EVENT: %s", diagnostic);
#else
    printf("DIAG-EVENT: %s\n", diagnostic);
#endif

#if defined(__GNUC__)
    extern void test_push_event(const char *event) __attribute__((weak));
#else
    extern void test_push_event(const char *event);
#endif
    if (chars_written > 0 && test_push_event != NULL) {
        printf("HOOK-DEBUG: test_push_event symbol present, forwarding event\n");
        test_push_event(diagnostic);
    } else {
        printf("HOOK-DEBUG: test_push_event not present or n<=0\n");
    }
    return CMD_SUCCESS;
}

static cmd_type_t parse_command_type(const char *token)
{
    if (strcasecmp(token, "SCAN") == 0) return CMD_TYPE_SCAN;
    if (strcasecmp(token, "AUDIO_STATUS") == 0) return CMD_TYPE_AUDIO_STATUS;
    if (strcasecmp(token, "CONNECT") == 0) return CMD_TYPE_CONNECT;
    if (strcasecmp(token, "CONNECT_NAME") == 0) return CMD_TYPE_CONNECT_NAME;
    if (strcasecmp(token, "DISCONNECT") == 0) return CMD_TYPE_DISCONNECT;
    if (strcasecmp(token, "PAIRED") == 0) return CMD_TYPE_PAIRED;
    if (strcasecmp(token, "SET_NAME") == 0) return CMD_TYPE_SET_NAME;
    if (strcasecmp(token, "START") == 0) return CMD_TYPE_START;
    if (strcasecmp(token, "STOP") == 0) return CMD_TYPE_STOP;
    if (strcasecmp(token, "VOLUME") == 0) return CMD_TYPE_VOLUME;
    if (strcasecmp(token, "MUTE") == 0) return CMD_TYPE_MUTE;
    if (strcasecmp(token, "UNMUTE") == 0) return CMD_TYPE_UNMUTE;
    if (strcasecmp(token, "STATUS") == 0) return CMD_TYPE_STATUS;
    if (strcasecmp(token, "VERSION") == 0) return CMD_TYPE_VERSION;
    if (strcasecmp(token, "RESET") == 0) return CMD_TYPE_RESET;
    if (strcasecmp(token, "DEBUG") == 0) return CMD_TYPE_DEBUG;
    if (strcasecmp(token, "SAMPLE_RATE") == 0) return CMD_TYPE_SAMPLE_RATE;
    if (strcasecmp(token, "MEM") == 0) return CMD_TYPE_MEM;
    if (strcasecmp(token, "SYNTH") == 0) return CMD_TYPE_SYNTH;
    if (strcasecmp(token, "I2S_CONFIG") == 0) return CMD_TYPE_I2S_CONFIG;
    if (strcasecmp(token, "I2S_PROBE") == 0) return CMD_TYPE_I2S_PROBE;
    if (strcasecmp(token, "I2S_RXTEST") == 0) return CMD_TYPE_I2S_RXTEST;
    if (strcasecmp(token, "I2S_CLKGEN") == 0) return CMD_TYPE_I2S_CLKGEN;
    if (strcasecmp(token, "BEEP") == 0) return CMD_TYPE_BEEP;
    if (strcasecmp(token, "PAIR") == 0) return CMD_TYPE_PAIR;
    if (strcasecmp(token, "CONFIRM_PIN") == 0) return CMD_TYPE_CONFIRM_PIN;
    if (strcasecmp(token, "ENTER_PIN") == 0) return CMD_TYPE_ENTER_PIN;
    if (strcasecmp(token, "SET_DEFAULT_PIN") == 0) return CMD_TYPE_SET_DEFAULT_PIN;
    if (strcasecmp(token, "UNPAIR") == 0) return CMD_TYPE_UNPAIR;
    if (strcasecmp(token, "UNPAIR_ALL") == 0) return CMD_TYPE_UNPAIR_ALL;
    if (strcasecmp(token, "PARTS") == 0) return CMD_TYPE_PARTS;
    if (strcasecmp(token, "AUDIO_AUTOSTART") == 0) return CMD_TYPE_AUDIO_AUTOSTART;
    if (strcasecmp(token, "LAST_MAC") == 0) return CMD_TYPE_LAST_MAC;
    if (strcasecmp(token, "DIAG") == 0) return CMD_TYPE_DIAG;
    if (strcasecmp(token, "UARTAUDIO") == 0) return CMD_TYPE_UARTAUDIO;
    if (strcasecmp(token, "SPANLOG") == 0) return CMD_TYPE_SPANLOG;
    if (strcasecmp(token, "HELP") == 0) return CMD_TYPE_HELP;
    if (strcasecmp(token, "HFP") == 0) return CMD_TYPE_HFP;
    return CMD_TYPE_UNKNOWN;
}

cmd_status_t cmd_parse(const char *cmd_str, cmd_context_t *ctx)
{
    if (cmd_str == NULL || ctx == NULL) return CMD_ERROR_INVALID_PARAM;
    memset(ctx, 0, sizeof(*ctx));

    char buf[512];
    cmd_safe_copy(buf, sizeof(buf), cmd_str);
    char *start = buf;
    while (*start != '\0' && isspace((unsigned char)*start)) ++start;
    if (*start == '\0') return CMD_ERROR_UNKNOWN;

    char *end = start + strlen(start) - 1;
    while (end >= start && isspace((unsigned char)*end)) {
        *end = '\0';
        --end;
    }
    if (*start == '\0') return CMD_ERROR_UNKNOWN;

    char *save = NULL;
    char *token = strtok_r(start, " \t", &save);
    if (token == NULL) return CMD_ERROR_UNKNOWN;
    ctx->type = parse_command_type(token);

    if (ctx->type == CMD_TYPE_CONNECT_NAME) {
        char *rest = save;
        while (rest != NULL && *rest != '\0' &&
               isspace((unsigned char)*rest)) {
            ++rest;
        }
        if (rest != NULL && *rest != '\0') {
            cmd_safe_copy(ctx->params[0], CMD_MAX_PARAM_LEN, rest);
            ctx->param_count = 1;
        }
        return CMD_SUCCESS;
    }

    int index = 0;
    while ((token = strtok_r(NULL, " \t", &save)) != NULL &&
           index < CMD_MAX_PARAMS) {
        cmd_safe_copy(ctx->params[index], CMD_MAX_PARAM_LEN, token);
        ++index;
    }
    ctx->param_count = index;
    return ctx->type == CMD_TYPE_UNKNOWN
        ? CMD_ERROR_UNKNOWN : CMD_SUCCESS;
}

static void cmd_process_port(size_t port_index)
{
    const int read_uart = s_cmd_ports[port_index];
    char *line_buf = s_cmd_line_buf[port_index];
    uint8_t read_buf[CMD_BUF_SIZE];

#if !defined(UNIT_TEST) && defined(ESP_PLATFORM)
    if (!uart_is_driver_installed(read_uart)) return;
#endif

    int bytes_read = uart_read_bytes(read_uart, read_buf,
                                     sizeof(read_buf) - 1U, 0);
    if (bytes_read <= 0) return;

    size_t to_copy = (size_t)bytes_read;
    if (s_cmd_line_len[port_index] + to_copy >= CMD_BUF_SIZE) {
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG,
                 "cmd_process: line buffer overflow on uart %d, resetting",
                 read_uart);
#endif
        s_cmd_line_len[port_index] = 0U;
        to_copy = CMD_BUF_SIZE - 1U;
    }
    memcpy(line_buf + s_cmd_line_len[port_index], read_buf, to_copy);
    s_cmd_line_len[port_index] += to_copy;
    line_buf[s_cmd_line_len[port_index]] = '\0';
    s_reply_uart = read_uart;

    char *start = line_buf;
    while (true) {
        size_t available =
            (size_t)(line_buf + s_cmd_line_len[port_index] - start);
        char *newline_pos = (char *)memchr(start, '\n', available);
        char *cr_pos = (char *)memchr(start, '\r', available);
        char *term = newline_pos != NULL ? newline_pos : cr_pos;
        if (term == NULL) break;

        *term = '\0';
        char *line_end = term - 1;
        while (line_end >= start && isspace((unsigned char)*line_end)) {
            *line_end = '\0';
            --line_end;
        }

        cmd_context_t ctx;
        cmd_status_t parse_status = cmd_parse(start, &ctx);
        if (parse_status == CMD_SUCCESS) {
            (void)cmd_execute(&ctx);
        } else if (parse_status == CMD_ERROR_UNKNOWN) {
            (void)cmd_send_response(CMD_STATUS_ERR, "UNKNOWN",
                                    "COMMAND_NOT_FOUND", NULL);
        }

        start = term + 1;
        while (start < line_buf + s_cmd_line_len[port_index] &&
               (*start == '\n' || *start == '\r')) {
            ++start;
        }
    }

    size_t remaining =
        (size_t)(line_buf + s_cmd_line_len[port_index] - start);
    if (remaining > 0U) memmove(line_buf, start, remaining);
    s_cmd_line_len[port_index] = remaining;
    line_buf[remaining] = '\0';
    s_reply_uart = CMD_UART_NUM;
}

cmd_status_t cmd_process(void)
{
    for (size_t index = 0U; index < CMD_PORT_COUNT; ++index) {
        if (s_cmd_ports[index] == CMD_UART_NUM &&
            uart_audio_is_streaming()) {
            continue;
        }
        cmd_process_port(index);
    }
    return CMD_SUCCESS;
}

cmd_status_t cmd_execute(const cmd_context_t *ctx)
{
    if (ctx == NULL) return CMD_ERROR_NOT_INITIALIZED;

    switch (ctx->type) {
    case CMD_TYPE_STATUS: return cmd_handle_status(ctx);
    case CMD_TYPE_AUDIO_STATUS: return cmd_handle_audio_status(ctx);
    case CMD_TYPE_MEM: return cmd_handle_mem(ctx);
    case CMD_TYPE_VERSION: return cmd_handle_version(ctx);
    case CMD_TYPE_RESET: return cmd_handle_reset(ctx);
    case CMD_TYPE_SCAN: return cmd_handle_scan(ctx);
    case CMD_TYPE_SYNTH: return cmd_handle_synth(ctx);
    case CMD_TYPE_DIAG: return cmd_handle_diag(ctx);
    case CMD_TYPE_BEEP: return cmd_handle_beep(ctx);
    case CMD_TYPE_CONNECT: return cmd_handle_connect(ctx);
    case CMD_TYPE_DISCONNECT: return cmd_handle_disconnect(ctx);
    case CMD_TYPE_START: return cmd_handle_start(ctx);
    case CMD_TYPE_STOP: return cmd_handle_stop(ctx);
    case CMD_TYPE_PARTS: return cmd_handle_parts(ctx);
    case CMD_TYPE_CONNECT_NAME: return cmd_handle_connect_name(ctx);
    case CMD_TYPE_CONFIRM_PIN: return cmd_handle_confirm_pin(ctx);
    case CMD_TYPE_ENTER_PIN: return cmd_handle_enter_pin(ctx);
    case CMD_TYPE_MUTE: return cmd_handle_mute(ctx);
    case CMD_TYPE_UNMUTE: return cmd_handle_unmute(ctx);
    case CMD_TYPE_VOLUME: return cmd_handle_volume(ctx);
    case CMD_TYPE_I2S_CONFIG: return cmd_handle_i2s_config(ctx);
    case CMD_TYPE_I2S_PROBE: return cmd_handle_i2s_probe(ctx);
    case CMD_TYPE_I2S_RXTEST: return cmd_handle_i2s_rxtest(ctx);
    case CMD_TYPE_I2S_CLKGEN: return cmd_handle_i2s_clkgen(ctx);
    case CMD_TYPE_AUDIO_AUTOSTART: return cmd_handle_audio_autostart(ctx);
    case CMD_TYPE_LAST_MAC: return cmd_handle_last_mac(ctx);
    case CMD_TYPE_PAIR: return cmd_handle_pair(ctx);
    case CMD_TYPE_PAIRED: return cmd_handle_paired(ctx);
    case CMD_TYPE_SAMPLE_RATE: return cmd_handle_sample_rate(ctx);
    case CMD_TYPE_SET_NAME: return cmd_handle_set_name(ctx);
    case CMD_TYPE_DEBUG: return cmd_handle_debug(ctx);
    case CMD_TYPE_SET_DEFAULT_PIN: return cmd_handle_set_default_pin(ctx);
    case CMD_TYPE_UNPAIR: return cmd_handle_unpair(ctx);
    case CMD_TYPE_UNPAIR_ALL: return cmd_handle_unpair_all(ctx);
    case CMD_TYPE_SPANLOG: return cmd_handle_spanlog(ctx);
    case CMD_TYPE_UARTAUDIO: return cmd_handle_uartaudio(ctx);
    case CMD_TYPE_HELP: return cmd_handle_help(ctx);
    case CMD_TYPE_HFP: return cmd_handle_hfp(ctx);
    default:
        (void)cmd_send_response(CMD_STATUS_INFO, "COMMAND",
                                "RECEIVED", "Not implemented yet");
        return CMD_SUCCESS;
    }
}
