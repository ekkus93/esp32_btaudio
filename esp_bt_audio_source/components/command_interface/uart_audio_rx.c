/**
 * uart_audio_rx.c — RX pump for UARTAUDIO streaming mode
 *
 * Pure logic (no FreeRTOS/driver dependencies): parser events route DATA
 * payloads into the uart_source staging ring, STOP triggers the drain,
 * and a run of consecutive CRC failures flags a link abort. Formatting
 * helpers produce the UA|FILL feedback line and STOPPED stats field so
 * the host tool and tests share one source of truth for the format.
 */

#include "uart_audio_rx.h"

#include <stdio.h>

static void rx_event_cb(const uart_af_event_t *evt, void *ctx)
{
    uart_audio_rx_t *rx = (uart_audio_rx_t *)ctx;

    switch (evt->type) {
    case UART_AF_EVT_FRAME_DATA:
        rx->consecutive_crc = 0;
        /* short write = ring overflow; uart_source counts it in stats */
        (void)uart_source_write(evt->payload, evt->len);
        break;

    case UART_AF_EVT_FRAME_STOP:
        rx->consecutive_crc = 0;
        rx->stop_seen = true;
        uart_source_request_drain();
        break;

    case UART_AF_EVT_ERR_CRC:
        rx->consecutive_crc++;
        break;

    case UART_AF_EVT_ERR_DESYNC:
    default:
        /* discarded bytes already tracked in parser stats */
        break;
    }
}

void uart_audio_rx_init(uart_audio_rx_t *rx)
{
    if (rx == NULL) {
        return;
    }
    uart_af_init(&rx->parser);
    rx->stop_seen = false;
    rx->consecutive_crc = 0;
}

void uart_audio_rx_feed(uart_audio_rx_t *rx, const uint8_t *data, size_t len)
{
    if (rx == NULL) {
        return;
    }
    uart_af_feed(&rx->parser, data, len, rx_event_cb, rx);
}

bool uart_audio_rx_stop_seen(const uart_audio_rx_t *rx)
{
    return (rx != NULL) && rx->stop_seen;
}

bool uart_audio_rx_crc_abort(const uart_audio_rx_t *rx)
{
    return (rx != NULL) && (rx->consecutive_crc > UART_AUDIO_CRC_ABORT_THRESHOLD);
}

int uart_audio_format_fill_line(char *buf, size_t buf_len,
                                const uart_audio_rx_t *rx)
{
    if (buf == NULL || buf_len == 0 || rx == NULL) {
        return -1;
    }

    uart_source_stats_t src;
    uart_source_get_stats(&src);

    return snprintf(buf, buf_len, "UA|FILL|%zu|%zu|%lu|%lu|%lu|%lu|%u\r\n",
                    src.ring_used,
                    src.ring_capacity,
                    (unsigned long)src.underrun_events,
                    (unsigned long)rx->parser.stats.crc_errors,
                    (unsigned long)rx->parser.stats.frames_lost,
                    (unsigned long)src.overflow_events,
                    (unsigned)rx->parser.last_seq);
}

int uart_audio_format_stopped_data(char *buf, size_t buf_len,
                                   const uart_audio_rx_t *rx,
                                   const uart_source_stats_t *src)
{
    if (buf == NULL || buf_len == 0 || rx == NULL || src == NULL) {
        return -1;
    }

    return snprintf(buf, buf_len,
                    "frames=%lu,bytes=%lu,crc=%lu,und=%lu,ovf=%lu,lost=%lu",
                    (unsigned long)rx->parser.stats.frames_data,
                    (unsigned long)src->bytes_in,
                    (unsigned long)rx->parser.stats.crc_errors,
                    (unsigned long)src->underrun_events,
                    (unsigned long)src->overflow_events,
                    (unsigned long)rx->parser.stats.frames_lost);
}
