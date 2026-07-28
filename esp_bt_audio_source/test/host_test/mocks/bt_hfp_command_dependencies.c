#include "cmd_handlers.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "audio_processor.h"
#include "uart_audio.h"

esp_err_t audio_processor_init(const audio_config_t *config)
{
    (void)config;
    return ESP_OK;
}

esp_err_t audio_processor_deinit(void)
{
    return ESP_OK;
}

void bt_manager_test_reset_btstate_mock(void) {}

bool uart_audio_is_streaming(void)
{
    return false;
}

void uart_audio_set_event_queue(void *queue)
{
    (void)queue;
}

int uart_audio_begin(int baud)
{
    (void)baud;
    return 0;
}

void uart_audio_test_reset(void) {}

uint64_t cmd_get_timestamp_ms(void)
{
    return 1234U;
}

void cmd_safe_copy(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) return;
    if (src == NULL) src = "";
    size_t length = strlen(src);
    if (length >= dst_size) length = dst_size - 1U;
    memcpy(dst, src, length);
    dst[length] = '\0';
}

int cmd_vsnprintf_safe(char *dst, size_t dst_size,
                       const char *fmt, va_list args)
{
    return vsnprintf(dst, dst_size, fmt, args);
}

int cmd_snprintf_safe(char *dst, size_t dst_size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int result = vsnprintf(dst, dst_size, fmt, args);
    va_end(args);
    return result;
}

#define STUB_HANDLER(name) \
    cmd_status_t name(const cmd_context_t *ctx) \
    { \
        (void)ctx; \
        return CMD_SUCCESS; \
    }

STUB_HANDLER(cmd_handle_status)
STUB_HANDLER(cmd_handle_audio_status)
STUB_HANDLER(cmd_handle_spanlog)
STUB_HANDLER(cmd_handle_mem)
STUB_HANDLER(cmd_handle_version)
STUB_HANDLER(cmd_handle_reset)
STUB_HANDLER(cmd_handle_help)
STUB_HANDLER(cmd_handle_scan)
STUB_HANDLER(cmd_handle_synth)
STUB_HANDLER(cmd_handle_uartaudio)
STUB_HANDLER(cmd_handle_diag)
STUB_HANDLER(cmd_handle_beep)
STUB_HANDLER(cmd_handle_start)
STUB_HANDLER(cmd_handle_stop)
STUB_HANDLER(cmd_handle_i2s_config)
STUB_HANDLER(cmd_handle_i2s_probe)
STUB_HANDLER(cmd_handle_i2s_rxtest)
STUB_HANDLER(cmd_handle_i2s_clkgen)
STUB_HANDLER(cmd_handle_sample_rate)
STUB_HANDLER(cmd_handle_volume)
STUB_HANDLER(cmd_handle_mute)
STUB_HANDLER(cmd_handle_unmute)
STUB_HANDLER(cmd_handle_audio_autostart)
STUB_HANDLER(cmd_handle_last_mac)
STUB_HANDLER(cmd_handle_parts)
STUB_HANDLER(cmd_handle_connect)
STUB_HANDLER(cmd_handle_connect_name)
STUB_HANDLER(cmd_handle_disconnect)
STUB_HANDLER(cmd_handle_pair)
STUB_HANDLER(cmd_handle_paired)
STUB_HANDLER(cmd_handle_confirm_pin)
STUB_HANDLER(cmd_handle_enter_pin)
STUB_HANDLER(cmd_handle_set_default_pin)
STUB_HANDLER(cmd_handle_unpair)
STUB_HANDLER(cmd_handle_unpair_all)
STUB_HANDLER(cmd_handle_set_name)
STUB_HANDLER(cmd_handle_debug)

#undef STUB_HANDLER

void cmd_test_capture_response(const char *line)
{
    (void)line;
}
