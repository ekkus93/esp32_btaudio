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
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "audio_processor.h"

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

/* UART driver event queue from main.c (see uart_audio_set_event_queue) */
static QueueHandle_t s_uart_evt_queue = NULL;

/* per-stream loss forensics, reset at stream start */
static uint32_t s_evt_fifo_ovf;
static uint32_t s_evt_buf_full;
static uint32_t s_evt_frame_err;
static uint32_t s_evt_parity_err;

void uart_audio_set_event_queue(void *queue)
{
    s_uart_evt_queue = (QueueHandle_t)queue;
}

static void ua_send_line(const char *line);

/* Drain pending driver events, counting loss/corruption causes and
 * emitting UA|ERR markers so the host can correlate them in time. */
static void ua_drain_uart_events(void)
{
    if (s_uart_evt_queue == NULL) {
        return;
    }
    uart_event_t evt;
    while (xQueueReceive(s_uart_evt_queue, &evt, 0) == pdTRUE) {
        switch (evt.type) {
        case UART_FIFO_OVF:
            s_evt_fifo_ovf++;
            ua_send_line("UA|ERR|FIFO_OVF\r\n");
            break;
        case UART_BUFFER_FULL:
            s_evt_buf_full++;
            ua_send_line("UA|ERR|BUF_FULL\r\n");
            break;
        case UART_FRAME_ERR:
            s_evt_frame_err++;
            break;
        case UART_PARITY_ERR:
            s_evt_parity_err++;
            break;
        default:
            break; /* UART_DATA etc. — normal traffic */
        }
    }
}

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

    /* quiet logs so binary frames don't interleave with log text.
     * BT_L2CAP goes fully silent: when the A2DP link congests it emits
     * ERROR-level lines dozens of times per second from the high-priority
     * BT task — console churn we can't afford while the 921600-baud RX
     * stream needs sub-1.4 ms interrupt service. Restored by the
     * wildcard reset at teardown. */
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set("BT_L2CAP", ESP_LOG_NONE);
    uart_wait_tx_done(CMD_UART_NUM, pdMS_TO_TICKS(200));
    uart_set_baudrate(CMD_UART_NUM, (uint32_t)s_stream_baud);
    uart_flush_input(CMD_UART_NUM);

    s_evt_fifo_ovf = 0;
    s_evt_buf_full = 0;
    s_evt_frame_err = 0;
    s_evt_parity_err = 0;
    if (s_uart_evt_queue != NULL) {
        xQueueReset(s_uart_evt_queue); /* discard stale text-mode events */
    }

    /* FIFO_OVF forensics showed the 128 B hardware FIFO overflowing when
     * BT critical sections mask interrupts past the default ISR trigger
     * (~120 full = only ~87 us of headroom at 921600 baud). Trigger at 32
     * instead: 96 B / ~1 ms of latency budget, ~3k extra interrupts/s. */
    uart_set_rx_full_threshold(CMD_UART_NUM, 32);

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
        ua_drain_uart_events();
        if (uart_audio_rx_crc_abort(&s_rx)) {
            break; /* link hosed */
        }
        feedback_ms += UA_READ_TIMEOUT_MS;
        if (feedback_ms >= CONFIG_UART_AUDIO_FEEDBACK_MS) {
            audio_read_rate_t rr = { 0 };
            (void)audio_processor_get_read_rate(&rr);
            char line[96];
            if (uart_audio_format_fill_line(line, sizeof(line), &s_rx,
                                            rr.rate_bps) > 0) {
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
    uart_set_rx_full_threshold(CMD_UART_NUM, 120); /* restore driver default */
    uart_flush_input(CMD_UART_NUM);
    esp_log_level_set("*", ESP_LOG_INFO);
    /* the wildcard reset above re-enables AUDIO_PROC INFO spam that main.c
     * deliberately suppresses at boot — re-apply that policy or the
     * per-read log flood drowns the command interface while A2DP runs */
    esp_log_level_set("AUDIO_PROC", ESP_LOG_WARN);

    uart_source_stop();

    ua_drain_uart_events(); /* pick up any events posted during drain */

    char data[224];
    int n_data = uart_audio_format_stopped_data(data, sizeof(data), &s_rx, &final_stats);
    if (n_data > 0) {
        snprintf(data + n_data, sizeof(data) - (size_t)n_data,
                 ",fifo_ovf=%lu,drv_full=%lu,frame_err=%lu,parity_err=%lu",
                 (unsigned long)s_evt_fifo_ovf,
                 (unsigned long)s_evt_buf_full,
                 (unsigned long)s_evt_frame_err,
                 (unsigned long)s_evt_parity_err);
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

void uart_audio_set_event_queue(void *queue)
{
    (void)queue;
}

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
    /* 22050 Hz stereo s16le needs 88.2 KB/s payload.
     * Only 921600 baud provides sufficient throughput (~115 KB/s).
     * 230400 (~28.8 KB/s) and 460800 (~57.6 KB/s) cannot carry it. */
    return (baud == 921600L);
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
                              "supported: 921600 (only)");
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
