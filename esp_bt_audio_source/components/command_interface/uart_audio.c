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
#include "uart_audio_rx.h"
#include "bt_manager.h"

static volatile bool s_streaming = false;

bool uart_audio_is_streaming(void)
{
    return s_streaming;
}

#if defined(ESP_PLATFORM) && !defined(UNIT_TEST)

/* ── reader task (device-only glue; logic lives in uart_audio_rx.c) ────── */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#define UA_RX_CHUNK_BYTES     1024
#define UA_READ_TIMEOUT_MS    20
#define UA_READY_PERIOD_MS    100
#define UA_READY_ABORT_MS     5000
#define UA_INACTIVITY_MS      2000
#define UA_DRAIN_TIMEOUT_MS   500
#define UA_TEXT_BAUD          115200

#ifndef CONFIG_UART_AUDIO_FEEDBACK_MS
#define CONFIG_UART_AUDIO_FEEDBACK_MS 250
#endif

static int s_stream_baud = CONFIG_UART_AUDIO_STREAM_BAUD;

static void ua_send_line(const char *line)
{
    uart_write_bytes(CMD_UART_NUM, line, strlen(line));
}

static void uart_audio_reader_task(void *arg)
{
    (void)arg;
    /* static: parser buffer (~2 KB) stays off the 4 KB task stack; only
     * one reader task can exist (s_streaming gate) */
    static uart_audio_rx_t s_rx;
    static uint8_t s_buf[UA_RX_CHUNK_BYTES];

    uart_audio_rx_init(&s_rx);

    /* quiet logs so binary frames don't interleave with log text */
    esp_log_level_set("*", ESP_LOG_WARN);
    uart_wait_tx_done(CMD_UART_NUM, pdMS_TO_TICKS(200));
    uart_set_baudrate(CMD_UART_NUM, (uint32_t)s_stream_baud);
    uart_flush_input(CMD_UART_NUM);

    uart_source_start((size_t)CONFIG_UART_AUDIO_STAGING_RB_KB * 1024U);

    /* READY beacon until the host's first byte arrives (host switches
     * baud ~50 ms after STARTING; beacon proves the new baud works) */
    bool aborted = false;
    uint32_t waited_ms = 0;
    uint32_t beacon_ms = UA_READY_PERIOD_MS; /* fire immediately */
    for (;;) {
        if (beacon_ms >= UA_READY_PERIOD_MS) {
            ua_send_line("UA|READY\r\n");
            beacon_ms = 0;
        }
        int n = uart_read_bytes(CMD_UART_NUM, s_buf, sizeof(s_buf),
                                pdMS_TO_TICKS(UA_READ_TIMEOUT_MS));
        if (n > 0) {
            uart_audio_rx_feed(&s_rx, s_buf, (size_t)n);
            break;
        }
        waited_ms += UA_READ_TIMEOUT_MS;
        beacon_ms += UA_READ_TIMEOUT_MS;
        if (waited_ms >= UA_READY_ABORT_MS) {
            aborted = true;
            break;
        }
    }

    /* main pump: read -> parse -> staging ring, with periodic UA|FILL */
    uint32_t idle_ms = 0;
    uint32_t feedback_ms = 0;
    while (!aborted && !uart_audio_rx_stop_seen(&s_rx)) {
        int n = uart_read_bytes(CMD_UART_NUM, s_buf, sizeof(s_buf),
                                pdMS_TO_TICKS(UA_READ_TIMEOUT_MS));
        if (n > 0) {
            idle_ms = 0;
            uart_audio_rx_feed(&s_rx, s_buf, (size_t)n);
        } else {
            idle_ms += UA_READ_TIMEOUT_MS;
            if (idle_ms >= UA_INACTIVITY_MS) {
                break; /* host died: auto-recover to text mode */
            }
        }
        if (uart_audio_rx_crc_abort(&s_rx)) {
            break; /* link hosed */
        }
        feedback_ms += UA_READ_TIMEOUT_MS;
        if (feedback_ms >= CONFIG_UART_AUDIO_FEEDBACK_MS) {
            char line[96];
            if (uart_audio_format_fill_line(line, sizeof(line), &s_rx) > 0) {
                ua_send_line(line);
            }
            feedback_ms = 0;
        }
    }

    /* play out buffered tail (bounded) */
    if (!uart_audio_rx_stop_seen(&s_rx)) {
        uart_source_request_drain();
    }
    uint32_t drain_ms = 0;
    while (uart_source_is_active() && drain_ms < UA_DRAIN_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(10));
        drain_ms += 10;
    }

    /* stats snapshot must precede uart_source_stop() */
    uart_source_stats_t final_stats;
    uart_source_get_stats(&final_stats);

    ua_send_line("UA|BYE\r\n");
    uart_wait_tx_done(CMD_UART_NUM, pdMS_TO_TICKS(200));
    uart_set_baudrate(CMD_UART_NUM, UA_TEXT_BAUD);
    uart_flush_input(CMD_UART_NUM);
    esp_log_level_set("*", ESP_LOG_INFO);

    uart_source_stop();

    char data[160];
    if (uart_audio_format_stopped_data(data, sizeof(data), &s_rx, &final_stats) > 0) {
        cmd_send_response("EVENT", "UARTAUDIO", "STOPPED", data);
    }

    /* clear flag LAST: cmd_process may resume polling from here on */
    s_streaming = false;
    vTaskDelete(NULL);
}

int uart_audio_begin(int baud)
{
    if (s_streaming) {
        return -1;
    }
    s_stream_baud = baud;
    s_streaming = true;
    BaseType_t ok = xTaskCreate(uart_audio_reader_task, "ua_reader", 4096,
                                NULL, configMAX_PRIORITIES - 3, NULL);
    if (ok != pdPASS) {
        s_streaming = false;
        ESP_LOGE("uart_audio", "reader task create failed");
        return -1;
    }
    return 0;
}

#else /* host build: no reader task; flag only */

int uart_audio_begin(int baud)
{
    (void)baud;
    if (s_streaming) {
        return -1;
    }
    s_streaming = true;
    return 0;
}

#endif /* ESP_PLATFORM && !UNIT_TEST */

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
    if (uart_audio_begin((int)baud) != 0) {
        /* host is waiting for the UA|READY beacon — tell it we bailed */
        cmd_send_response("ERR", "UARTAUDIO", "START_FAILED",
                          "reader task unavailable");
    }
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
