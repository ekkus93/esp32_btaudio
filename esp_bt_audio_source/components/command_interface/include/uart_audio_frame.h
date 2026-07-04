/**
 * uart_audio_frame.h — byte-stream frame parser for UART audio streaming
 *
 * Wire format (little-endian, 8-byte header):
 *   [0..1]  magic   0xA5 0x5A
 *   [2]     type    0x01 = DATA, 0x02 = STOP
 *   [3]     seq     uint8, wraps (DATA only; 0 for STOP)
 *   [4..5]  len     payload bytes, LE (DATA: 4..2048, multiple of 4; STOP: 0)
 *   [6..7]  crc16   CRC-16/CCITT-FALSE over header bytes [2..5] + payload, LE
 *   [8..]   payload raw s16le stereo PCM
 *
 * The parser is a byte-at-a-time state machine: feed boundaries are
 * irrelevant. On a bad header or CRC it re-hunts for the magic pair in
 * subsequent input ("scan forward"); discarded bytes and lost frames are
 * tracked in stats. No dynamic allocation, no ESP-IDF dependencies —
 * fully host-testable.
 */

#ifndef UART_AUDIO_FRAME_H
#define UART_AUDIO_FRAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UART_AF_MAGIC0        0xA5U
#define UART_AF_MAGIC1        0x5AU
#define UART_AF_TYPE_DATA     0x01U
#define UART_AF_TYPE_STOP     0x02U
#define UART_AF_HEADER_BYTES  8U
#define UART_AF_MAX_PAYLOAD   2048U

typedef enum {
    UART_AF_EVT_FRAME_DATA = 0, /**< valid DATA frame (payload/len/seq set) */
    UART_AF_EVT_FRAME_STOP,     /**< valid STOP frame */
    UART_AF_EVT_ERR_CRC,        /**< full frame received but CRC mismatched */
    UART_AF_EVT_ERR_DESYNC,     /**< started discarding bytes to re-find magic */
} uart_af_event_type_t;

typedef struct {
    uart_af_event_type_t type;
    const uint8_t *payload; /**< DATA only; valid only during the callback */
    uint16_t len;           /**< DATA payload length in bytes */
    uint8_t seq;            /**< DATA sequence number */
} uart_af_event_t;

typedef void (*uart_af_event_cb_t)(const uart_af_event_t *evt, void *ctx);

typedef struct {
    uint32_t frames_data;   /**< valid DATA frames delivered */
    uint32_t frames_stop;   /**< valid STOP frames delivered */
    uint32_t crc_errors;    /**< frames dropped for CRC mismatch */
    uint32_t desync_bytes;  /**< bytes discarded while hunting for magic */
    uint32_t frames_lost;   /**< frames missing per seq-gap accounting */
} uart_af_stats_t;

typedef struct {
    /* internal state machine — treat as opaque outside uart_audio_frame.c */
    uint8_t state;
    uint8_t hdr[6];                        /* type, seq, len_lo, len_hi, crc_lo, crc_hi */
    uint16_t hdr_pos;
    uint8_t payload[UART_AF_MAX_PAYLOAD];
    uint16_t payload_pos;
    uint16_t expect_len;
    uint8_t hunting;                       /* inside a discard run (ERR_DESYNC already emitted) */
    uint8_t have_seq;
    uint8_t last_seq;
    uart_af_stats_t stats;
} uart_af_parser_t;

/** Reset parser to initial state (also clears stats and seq tracking). */
void uart_af_init(uart_af_parser_t *p);

/**
 * Feed n bytes into the parser. cb is invoked synchronously for every
 * event; evt->payload points into parser-owned storage and is only valid
 * for the duration of the callback. Any feed granularity is accepted,
 * down to one byte at a time.
 */
void uart_af_feed(uart_af_parser_t *p, const uint8_t *data, size_t n,
                  uart_af_event_cb_t cb, void *ctx);

/**
 * CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, xorout 0).
 * Exposed so the host-side streamer and tests share one reference.
 * Check value: crc16("123456789") == 0x29B1.
 */
uint16_t uart_af_crc16(const uint8_t *data, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* UART_AUDIO_FRAME_H */
