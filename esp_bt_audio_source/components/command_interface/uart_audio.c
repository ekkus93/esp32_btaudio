/**
 * uart_audio.c — UARTAUDIO command handler + streaming-mode flag
 *
 * See uart_audio.h for the RX ownership handoff contract. The reader
 * task itself (baud switch, frame RX, feedback, timeouts) is UARTAUDIO-6;
 * until then uart_audio_begin() only flips the mode flag.
 */

#include "uart_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "commands_priv.h"
#include "cmd_handlers.h"
#include "uart_source.h"
#include "bt_manager.h"

static volatile bool s_streaming = false;

bool uart_audio_is_streaming(void)
{
    return s_streaming;
}

int uart_audio_begin(int baud)
{
    (void)baud;
    if (s_streaming) {
        return -1;
    }
    s_streaming = true;
#if defined(ESP_PLATFORM) && !defined(UNIT_TEST)
    /* UARTAUDIO-6: spawn uart_audio_reader task here (uses baud). */
#endif
    return 0;
}

#if defined(UNIT_TEST)
void uart_audio_test_reset(void)
{
    s_streaming = false;
}
#endif

static bool baud_is_supported(long baud)
{
    /* 22050 Hz stereo s16 needs 88.2 KB/s payload; anything under
     * 230400 baud can't carry it even before framing overhead. */
    return (baud == 230400L) || (baud == 460800L) || (baud == 921600L);
}

static const char *state_name(uart_source_state_t state)
{
    switch (state) {
        case UART_SOURCE_STATE_PREBUFFER: return "PREBUFFER";
        case UART_SOURCE_STATE_ACTIVE:    return "ACTIVE";
        case UART_SOURCE_STATE_DRAINING:  return "DRAINING";
        case UART_SOURCE_STATE_INACTIVE:
        default:                          return "INACTIVE";
    }
}

static cmd_status_t handle_start(const cmd_context_t *ctx)
{
    if (s_streaming || uart_source_is_active()) {
        cmd_send_response("ERR", "UARTAUDIO", "BUSY", "stream already in progress");
        return CMD_SUCCESS;
    }

    long baud = CONFIG_UART_AUDIO_STREAM_BAUD;
    if (ctx->param_count >= 2) {
        char *endp = NULL;
        baud = strtol(ctx->params[1], &endp, 10);
        if (endp == NULL || *endp != '\0' || !baud_is_supported(baud)) {
            cmd_send_response("ERR", "UARTAUDIO", "BAD_BAUD",
                              "supported: 230400, 460800, 921600");
            return CMD_SUCCESS;
        }
    }

    char data[128];
    snprintf(data, sizeof(data), "baud=%ld,frame=%u,ring=%u,a2dp=%d",
             baud,
             (unsigned)UART_AUDIO_FRAME_PAYLOAD_BYTES,
             (unsigned)(CONFIG_UART_AUDIO_STAGING_RB_KB * 1024U),
             bt_manager_is_connected() ? 1 : 0);

    /* Response must be queued at 115200 BEFORE streaming mode engages —
     * once the flag is set the reader task owns the UART. */
    cmd_send_response("OK", "UARTAUDIO", "STARTING", data);
    (void)uart_audio_begin((int)baud);
    return CMD_SUCCESS;
}

static cmd_status_t handle_status(void)
{
    uart_source_stats_t st;
    uart_source_get_stats(&st);

    char data[192];
    snprintf(data, sizeof(data),
             "streaming=%d,state=%s,used=%zu,cap=%zu,und=%lu,ovf=%lu,in=%lu,out=%lu",
             s_streaming ? 1 : 0,
             state_name(st.state),
             st.ring_used, st.ring_capacity,
             (unsigned long)st.underrun_events,
             (unsigned long)st.overflow_events,
             (unsigned long)st.bytes_in,
             (unsigned long)st.bytes_out);
    cmd_send_response("OK", "UARTAUDIO", "STATUS", data);
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_uartaudio(const cmd_context_t *ctx)
{
    if (ctx == NULL) {
        return CMD_ERROR_INVALID_PARAM;
    }
    if (ctx->param_count < 1) {
        cmd_send_response("ERR", "UARTAUDIO", "BAD_ARGS",
                          "usage: UARTAUDIO START [baud] | STATUS | STOP");
        return CMD_SUCCESS;
    }

    const char *sub = ctx->params[0];
    if (strcasecmp(sub, "START") == 0) {
        return handle_start(ctx);
    }
    if (strcasecmp(sub, "STATUS") == 0) {
        return handle_status();
    }
    if (strcasecmp(sub, "STOP") == 0) {
        /* In text mode there is nothing to stop; during streaming the
         * host sends the in-band STOP frame instead (cmd_process is
         * gated, so this handler can never run mid-stream). */
        cmd_send_response("ERR", "UARTAUDIO", "NOT_STREAMING",
                          "stop is the in-band STOP frame during streaming");
        return CMD_SUCCESS;
    }

    cmd_send_response("ERR", "UARTAUDIO", "BAD_ARGS",
                      "usage: UARTAUDIO START [baud] | STATUS | STOP");
    return CMD_SUCCESS;
}
