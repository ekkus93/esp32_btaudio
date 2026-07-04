/**
 * uart_audio_rx.h — RX pump for UARTAUDIO streaming mode
 *
 * Pure, host-testable core of the reader task: pushes raw UART bytes
 * through the frame parser into the uart_source staging ring, tracks the
 * in-band STOP frame and consecutive-CRC abort condition, and formats
 * the UA|FILL feedback and STOPPED stats lines. The reader task itself
 * (baud switch, timing, FreeRTOS glue) lives in uart_audio.c.
 */

#ifndef UART_AUDIO_RX_H
#define UART_AUDIO_RX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "uart_audio_frame.h"
#include "uart_source.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Abort streaming after MORE THAN this many consecutive CRC failures
 * (a run this long means the link is hosed, not a stray glitch). */
#define UART_AUDIO_CRC_ABORT_THRESHOLD 8U

typedef struct {
    uart_af_parser_t parser;
    bool stop_seen;              /**< in-band STOP frame received */
    uint32_t consecutive_crc;    /**< CRC failures since last good frame */
} uart_audio_rx_t;

/** Reset pump state (parser, stop flag, CRC run counter). */
void uart_audio_rx_init(uart_audio_rx_t *rx);

/**
 * Feed raw UART bytes. Valid DATA payloads go to uart_source_write();
 * a STOP frame sets stop_seen and requests uart_source drain. Any feed
 * granularity is accepted.
 */
void uart_audio_rx_feed(uart_audio_rx_t *rx, const uint8_t *data, size_t len);

bool uart_audio_rx_stop_seen(const uart_audio_rx_t *rx);

/** True once consecutive CRC failures exceed UART_AUDIO_CRC_ABORT_THRESHOLD. */
bool uart_audio_rx_crc_abort(const uart_audio_rx_t *rx);

/**
 * Format the periodic feedback line:
 *   UA|FILL|<used>|<cap>|<und>|<crc>|<lost>|<ovf>|<seq>|<a2dp_bps>\r\n
 * a2dp_pull_bps is the device's current A2DP consumption rate in bytes/s
 * (audio_processor_get_read_rate; 176400 = 44.1 kHz stereo real time,
 * 0 = unknown/warming up). Returns the line length (snprintf semantics).
 */
int uart_audio_format_fill_line(char *buf, size_t buf_len,
                                const uart_audio_rx_t *rx,
                                uint32_t a2dp_pull_bps);

/**
 * Format the EVENT|UARTAUDIO|STOPPED data field:
 *   frames=..,bytes=..,crc=..,und=..,ovf=..,lost=..
 * src is a stats snapshot taken BEFORE uart_source_stop().
 */
int uart_audio_format_stopped_data(char *buf, size_t buf_len,
                                   const uart_audio_rx_t *rx,
                                   const uart_source_stats_t *src);

#ifdef __cplusplus
}
#endif

#endif /* UART_AUDIO_RX_H */
