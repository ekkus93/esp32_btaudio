/**
 * uart_audio_frame.c — byte-stream frame parser for UART audio streaming
 *
 * See uart_audio_frame.h for the wire format. Pure C, no ESP-IDF
 * dependencies; the same code runs on device and in host tests.
 *
 * Resync policy: a CRC failure on a complete frame leaves the stream
 * aligned (exactly one frame was consumed), so parsing resumes at the
 * next byte. A bad header (unknown type / invalid len) means we synced
 * on a false magic or the stream is corrupt — the parser drops the
 * buffered header bytes and scans forward for the next magic pair.
 * Consumed header bytes are not re-scanned: the fields are validated
 * eagerly enough that a false sync costs at most one skipped frame,
 * which seq-gap accounting records.
 */

#include "uart_audio_frame.h"

#include <string.h>

enum {
    ST_SYNC0 = 0, /* hunting for 0xA5 */
    ST_SYNC1,     /* saw 0xA5, expecting 0x5A */
    ST_HDR,       /* collecting type/seq/len/crc (6 bytes) */
    ST_PAYLOAD,   /* collecting expect_len payload bytes */
};

/* header byte offsets within p->hdr[] (frame bytes 2..7) */
#define HDR_TYPE    0
#define HDR_SEQ     1
#define HDR_LEN_LO  2
#define HDR_LEN_HI  3
#define HDR_CRC_LO  4
#define HDR_CRC_HI  5
#define HDR_BYTES   6
#define MAGIC_BYTES 2

#define UART_AF_MIN_PAYLOAD 4U

static uint16_t crc16_update(uint16_t crc, const uint8_t *data, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int bit = 0; bit < 8; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint16_t uart_af_crc16(const uint8_t *data, size_t n)
{
    return crc16_update(0xFFFFU, data, n);
}

void uart_af_init(uart_af_parser_t *p)
{
    memset(p, 0, sizeof(*p));
    p->state = ST_SYNC0;
}

static void emit(uart_af_event_type_t type, const uint8_t *payload,
                 uint16_t len, uint8_t seq, uart_af_event_cb_t cb, void *ctx)
{
    if (cb != NULL) {
        uart_af_event_t evt = {
            .type = type,
            .payload = payload,
            .len = len,
            .seq = seq,
        };
        cb(&evt, ctx);
    }
}

/* Record discarded bytes; emit ERR_DESYNC once per contiguous discard run. */
static void note_discard(uart_af_parser_t *p, uint32_t count,
                         uart_af_event_cb_t cb, void *ctx)
{
    p->stats.desync_bytes += count;
    if (p->hunting == 0U) {
        p->hunting = 1U;
        emit(UART_AF_EVT_ERR_DESYNC, NULL, 0, 0, cb, ctx);
    }
}

static int header_is_valid(uint8_t type, uint16_t len)
{
    if (type == UART_AF_TYPE_DATA) {
        return (len >= UART_AF_MIN_PAYLOAD) && (len <= UART_AF_MAX_PAYLOAD) &&
               ((len % 4U) == 0U);
    }
    if (type == UART_AF_TYPE_STOP) {
        return len == 0U;
    }
    return 0;
}

static void account_seq(uart_af_parser_t *p, uint8_t seq)
{
    if (p->have_seq != 0U) {
        uint8_t expected = (uint8_t)(p->last_seq + 1U);
        p->stats.frames_lost += (uint8_t)(seq - expected);
    }
    p->last_seq = seq;
    p->have_seq = 1U;
}

static void finish_frame(uart_af_parser_t *p, uart_af_event_cb_t cb, void *ctx)
{
    const uint8_t type = p->hdr[HDR_TYPE];
    const uint8_t seq = p->hdr[HDR_SEQ];
    const uint16_t wire_crc =
        (uint16_t)(p->hdr[HDR_CRC_LO] | ((uint16_t)p->hdr[HDR_CRC_HI] << 8));

    uint16_t crc = crc16_update(0xFFFFU, p->hdr, 4); /* type, seq, len */
    crc = crc16_update(crc, p->payload, p->expect_len);

    if (crc != wire_crc) {
        p->stats.crc_errors++;
        emit(UART_AF_EVT_ERR_CRC, NULL, 0, 0, cb, ctx);
    } else if (type == UART_AF_TYPE_STOP) {
        p->stats.frames_stop++;
        emit(UART_AF_EVT_FRAME_STOP, NULL, 0, 0, cb, ctx);
    } else {
        p->stats.frames_data++;
        account_seq(p, seq);
        emit(UART_AF_EVT_FRAME_DATA, p->payload, p->expect_len, seq, cb, ctx);
    }
    p->state = ST_SYNC0;
}

static void push_byte(uart_af_parser_t *p, uint8_t b,
                      uart_af_event_cb_t cb, void *ctx)
{
    switch (p->state) {
    case ST_SYNC0:
        if (b == UART_AF_MAGIC0) {
            p->state = ST_SYNC1;
        } else {
            note_discard(p, 1, cb, ctx);
        }
        break;

    case ST_SYNC1:
        if (b == UART_AF_MAGIC1) {
            p->state = ST_HDR;
            p->hdr_pos = 0;
            p->hunting = 0U;
        } else {
            /* the pending 0xA5 was garbage */
            note_discard(p, 1, cb, ctx);
            if (b != UART_AF_MAGIC0) {
                note_discard(p, 1, cb, ctx);
                p->state = ST_SYNC0;
            } /* else: b may start a real magic pair — stay in ST_SYNC1 */
        }
        break;

    case ST_HDR:
        p->hdr[p->hdr_pos++] = b;
        if (p->hdr_pos == HDR_BYTES) {
            const uint16_t len = (uint16_t)(p->hdr[HDR_LEN_LO] |
                                            ((uint16_t)p->hdr[HDR_LEN_HI] << 8));
            if (!header_is_valid(p->hdr[HDR_TYPE], len)) {
                /* false sync or corrupt header: drop magic + header, re-hunt */
                note_discard(p, MAGIC_BYTES + HDR_BYTES, cb, ctx);
                p->state = ST_SYNC0;
            } else if (len == 0U) {
                p->expect_len = 0;
                finish_frame(p, cb, ctx); /* STOP: no payload to collect */
            } else {
                p->expect_len = len;
                p->payload_pos = 0;
                p->state = ST_PAYLOAD;
            }
        }
        break;

    case ST_PAYLOAD:
        p->payload[p->payload_pos++] = b;
        if (p->payload_pos == p->expect_len) {
            finish_frame(p, cb, ctx);
        }
        break;

    default:
        p->state = ST_SYNC0;
        break;
    }
}

void uart_af_feed(uart_af_parser_t *p, const uint8_t *data, size_t n,
                  uart_af_event_cb_t cb, void *ctx)
{
    if ((p == NULL) || (data == NULL)) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        push_byte(p, data[i], cb, ctx);
    }
}
