/**
 * test_uart_audio_frame.c — unit tests for the UART audio frame parser
 *
 * Covers:
 *   - uart_af_crc16()  CRC-16/CCITT-FALSE check vector
 *   - whole frame in a single feed
 *   - byte-at-a-time feeds (feed-boundary independence)
 *   - CRC corruption -> ERR_CRC, following frames still parse (alignment kept)
 *   - garbage prefix -> ERR_DESYNC once + desync_bytes, then frame parses
 *   - magic pair inside payload -> no spurious resync
 *   - STOP frame; STOP with nonzero len rejected
 *   - oversize / misaligned len rejected as bad header, recovery afterwards
 *   - seq wrap 255->0 without loss; seq gaps counted in frames_lost
 */

#include <string.h>
#include <stdio.h>
#include "unity.h"
#include "uart_audio_frame.h"

/* ── event capture ──────────────────────────────────────────────────────── */

#define MAX_EVENTS 32

typedef struct {
    uart_af_event_type_t type;
    uint8_t payload[UART_AF_MAX_PAYLOAD];
    uint16_t len;
    uint8_t seq;
} captured_event_t;

static captured_event_t s_events[MAX_EVENTS];
static int s_event_count;

static void capture_cb(const uart_af_event_t *evt, void *ctx)
{
    (void)ctx;
    TEST_ASSERT_TRUE_MESSAGE(s_event_count < MAX_EVENTS, "event overflow");
    captured_event_t *e = &s_events[s_event_count++];
    e->type = evt->type;
    e->len = evt->len;
    e->seq = evt->seq;
    if (evt->type == UART_AF_EVT_FRAME_DATA && evt->payload != NULL) {
        memcpy(e->payload, evt->payload, evt->len);
    }
}

static uart_af_parser_t s_parser;

void setUp(void)
{
    memset(s_events, 0, sizeof(s_events));
    s_event_count = 0;
    uart_af_init(&s_parser);
}

void tearDown(void) {}

/* ── helpers ────────────────────────────────────────────────────────────── */

/* Build a frame into buf; returns total length. crc_delta lets tests corrupt
 * the stored CRC (0 = valid frame). */
static size_t build_frame(uint8_t *buf, uint8_t type, uint8_t seq,
                          const uint8_t *payload, uint16_t len,
                          uint16_t crc_delta)
{
    buf[0] = UART_AF_MAGIC0;
    buf[1] = UART_AF_MAGIC1;
    buf[2] = type;
    buf[3] = seq;
    buf[4] = (uint8_t)(len & 0xFF);
    buf[5] = (uint8_t)(len >> 8);
    if (len > 0) {
        memcpy(&buf[8], payload, len);
    }
    /* CRC over bytes[2..5] + payload */
    uint8_t crc_src[4 + UART_AF_MAX_PAYLOAD];
    memcpy(crc_src, &buf[2], 4);
    if (len > 0) {
        memcpy(&crc_src[4], payload, len);
    }
    uint16_t crc = (uint16_t)(uart_af_crc16(crc_src, 4U + len) + crc_delta);
    buf[6] = (uint8_t)(crc & 0xFF);
    buf[7] = (uint8_t)(crc >> 8);
    return (size_t)UART_AF_HEADER_BYTES + len;
}

static void fill_pattern(uint8_t *dst, uint16_t len, uint8_t base)
{
    for (uint16_t i = 0; i < len; i++) {
        dst[i] = (uint8_t)(base + i);
    }
}

/* ── CRC ────────────────────────────────────────────────────────────────── */

void test_crc16_ccitt_false_check_vector(void)
{
    /* Canonical CRC-16/CCITT-FALSE check value */
    TEST_ASSERT_EQUAL_HEX16(0x29B1, uart_af_crc16((const uint8_t *)"123456789", 9));
}

void test_crc16_empty_is_init_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, uart_af_crc16((const uint8_t *)"", 0));
}

/* ── whole-frame parsing ────────────────────────────────────────────────── */

void test_single_data_frame_one_feed(void)
{
    uint8_t payload[64];
    fill_pattern(payload, sizeof(payload), 0x10);
    uint8_t frame[UART_AF_HEADER_BYTES + sizeof(payload)];
    size_t n = build_frame(frame, UART_AF_TYPE_DATA, 7, payload, sizeof(payload), 0);

    uart_af_feed(&s_parser, frame, n, capture_cb, NULL);

    TEST_ASSERT_EQUAL_INT(1, s_event_count);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_FRAME_DATA, s_events[0].type);
    TEST_ASSERT_EQUAL_UINT16(sizeof(payload), s_events[0].len);
    TEST_ASSERT_EQUAL_UINT8(7, s_events[0].seq);
    TEST_ASSERT_EQUAL_MEMORY(payload, s_events[0].payload, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT32(1, s_parser.stats.frames_data);
    TEST_ASSERT_EQUAL_UINT32(0, s_parser.stats.crc_errors);
    TEST_ASSERT_EQUAL_UINT32(0, s_parser.stats.desync_bytes);
}

void test_max_payload_frame(void)
{
    static uint8_t payload[UART_AF_MAX_PAYLOAD];
    fill_pattern(payload, UART_AF_MAX_PAYLOAD, 0);
    static uint8_t frame[UART_AF_HEADER_BYTES + UART_AF_MAX_PAYLOAD];
    size_t n = build_frame(frame, UART_AF_TYPE_DATA, 0, payload, UART_AF_MAX_PAYLOAD, 0);

    uart_af_feed(&s_parser, frame, n, capture_cb, NULL);

    TEST_ASSERT_EQUAL_INT(1, s_event_count);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_FRAME_DATA, s_events[0].type);
    TEST_ASSERT_EQUAL_UINT16(UART_AF_MAX_PAYLOAD, s_events[0].len);
    TEST_ASSERT_EQUAL_MEMORY(payload, s_events[0].payload, UART_AF_MAX_PAYLOAD);
}

void test_byte_at_a_time_feed(void)
{
    uint8_t payload[32];
    fill_pattern(payload, sizeof(payload), 0xA0);
    uint8_t frame[UART_AF_HEADER_BYTES + sizeof(payload)];
    size_t n = build_frame(frame, UART_AF_TYPE_DATA, 3, payload, sizeof(payload), 0);

    for (size_t i = 0; i < n; i++) {
        uart_af_feed(&s_parser, &frame[i], 1, capture_cb, NULL);
    }

    TEST_ASSERT_EQUAL_INT(1, s_event_count);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_FRAME_DATA, s_events[0].type);
    TEST_ASSERT_EQUAL_UINT8(3, s_events[0].seq);
    TEST_ASSERT_EQUAL_MEMORY(payload, s_events[0].payload, sizeof(payload));
}

void test_split_feed_across_payload_boundary(void)
{
    uint8_t payload[48];
    fill_pattern(payload, sizeof(payload), 0x55);
    uint8_t frame[UART_AF_HEADER_BYTES + sizeof(payload)];
    size_t n = build_frame(frame, UART_AF_TYPE_DATA, 1, payload, sizeof(payload), 0);

    /* split mid-header and mid-payload */
    uart_af_feed(&s_parser, frame, 5, capture_cb, NULL);
    TEST_ASSERT_EQUAL_INT(0, s_event_count);
    uart_af_feed(&s_parser, frame + 5, 20, capture_cb, NULL);
    TEST_ASSERT_EQUAL_INT(0, s_event_count);
    uart_af_feed(&s_parser, frame + 25, n - 25, capture_cb, NULL);

    TEST_ASSERT_EQUAL_INT(1, s_event_count);
    TEST_ASSERT_EQUAL_MEMORY(payload, s_events[0].payload, sizeof(payload));
}

void test_back_to_back_frames_single_feed(void)
{
    uint8_t p1[16], p2[16];
    fill_pattern(p1, sizeof(p1), 0x01);
    fill_pattern(p2, sizeof(p2), 0x81);
    uint8_t stream[2 * (UART_AF_HEADER_BYTES + 16)];
    size_t n1 = build_frame(stream, UART_AF_TYPE_DATA, 10, p1, sizeof(p1), 0);
    size_t n2 = build_frame(stream + n1, UART_AF_TYPE_DATA, 11, p2, sizeof(p2), 0);

    uart_af_feed(&s_parser, stream, n1 + n2, capture_cb, NULL);

    TEST_ASSERT_EQUAL_INT(2, s_event_count);
    TEST_ASSERT_EQUAL_UINT8(10, s_events[0].seq);
    TEST_ASSERT_EQUAL_UINT8(11, s_events[1].seq);
    TEST_ASSERT_EQUAL_MEMORY(p2, s_events[1].payload, sizeof(p2));
}

/* ── STOP frames ────────────────────────────────────────────────────────── */

void test_stop_frame(void)
{
    uint8_t frame[UART_AF_HEADER_BYTES];
    size_t n = build_frame(frame, UART_AF_TYPE_STOP, 0, NULL, 0, 0);

    uart_af_feed(&s_parser, frame, n, capture_cb, NULL);

    TEST_ASSERT_EQUAL_INT(1, s_event_count);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_FRAME_STOP, s_events[0].type);
    TEST_ASSERT_EQUAL_UINT32(1, s_parser.stats.frames_stop);
}

void test_stop_frame_with_nonzero_len_rejected(void)
{
    uint8_t payload[8];
    fill_pattern(payload, sizeof(payload), 0);
    uint8_t frame[UART_AF_HEADER_BYTES + sizeof(payload)];
    build_frame(frame, UART_AF_TYPE_STOP, 0, payload, sizeof(payload), 0);

    uart_af_feed(&s_parser, frame, UART_AF_HEADER_BYTES + sizeof(payload),
                 capture_cb, NULL);

    TEST_ASSERT_EQUAL_UINT32(0, s_parser.stats.frames_stop);
    TEST_ASSERT_EQUAL_UINT32(0, s_parser.stats.frames_data);
    /* must have signalled a desync (bad header) */
    TEST_ASSERT_TRUE(s_event_count >= 1);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_ERR_DESYNC, s_events[0].type);
}

/* ── CRC corruption and resync ──────────────────────────────────────────── */

void test_crc_corruption_then_recovery(void)
{
    uint8_t p1[32], p2[32], p3[32];
    fill_pattern(p1, sizeof(p1), 1);
    fill_pattern(p2, sizeof(p2), 2);
    fill_pattern(p3, sizeof(p3), 3);

    uint8_t stream[3 * (UART_AF_HEADER_BYTES + 32)];
    size_t n1 = build_frame(stream, UART_AF_TYPE_DATA, 0, p1, sizeof(p1), 1); /* bad CRC */
    size_t n2 = build_frame(stream + n1, UART_AF_TYPE_DATA, 1, p2, sizeof(p2), 0);
    size_t n3 = build_frame(stream + n1 + n2, UART_AF_TYPE_DATA, 2, p3, sizeof(p3), 0);

    uart_af_feed(&s_parser, stream, n1 + n2 + n3, capture_cb, NULL);

    TEST_ASSERT_EQUAL_INT(3, s_event_count);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_ERR_CRC, s_events[0].type);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_FRAME_DATA, s_events[1].type);
    TEST_ASSERT_EQUAL_UINT8(1, s_events[1].seq);
    TEST_ASSERT_EQUAL_MEMORY(p2, s_events[1].payload, sizeof(p2));
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_FRAME_DATA, s_events[2].type);
    TEST_ASSERT_EQUAL_UINT32(1, s_parser.stats.crc_errors);
    TEST_ASSERT_EQUAL_UINT32(2, s_parser.stats.frames_data);
}

void test_corrupt_payload_byte_gives_crc_error(void)
{
    uint8_t payload[16];
    fill_pattern(payload, sizeof(payload), 0x40);
    uint8_t frame[UART_AF_HEADER_BYTES + sizeof(payload)];
    size_t n = build_frame(frame, UART_AF_TYPE_DATA, 0, payload, sizeof(payload), 0);
    frame[12] ^= 0xFF; /* corrupt one payload byte, CRC now wrong */

    uart_af_feed(&s_parser, frame, n, capture_cb, NULL);

    TEST_ASSERT_EQUAL_INT(1, s_event_count);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_ERR_CRC, s_events[0].type);
    TEST_ASSERT_EQUAL_UINT32(1, s_parser.stats.crc_errors);
}

/* ── garbage / desync handling ──────────────────────────────────────────── */

void test_garbage_prefix_then_frame(void)
{
    const uint8_t garbage[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x5A, 0x66 };
    uint8_t payload[16];
    fill_pattern(payload, sizeof(payload), 0x77);
    uint8_t frame[UART_AF_HEADER_BYTES + sizeof(payload)];
    size_t n = build_frame(frame, UART_AF_TYPE_DATA, 5, payload, sizeof(payload), 0);

    uart_af_feed(&s_parser, garbage, sizeof(garbage), capture_cb, NULL);
    uart_af_feed(&s_parser, frame, n, capture_cb, NULL);

    /* exactly one ERR_DESYNC for the whole garbage run, then the frame */
    TEST_ASSERT_EQUAL_INT(2, s_event_count);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_ERR_DESYNC, s_events[0].type);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_FRAME_DATA, s_events[1].type);
    TEST_ASSERT_EQUAL_UINT8(5, s_events[1].seq);
    TEST_ASSERT_EQUAL_UINT32(sizeof(garbage), s_parser.stats.desync_bytes);
}

void test_lone_magic0_prefix_then_frame(void)
{
    /* A stray 0xA5 followed by a real frame: the parser must not consume
     * the real frame's first magic byte as the second byte of a bogus pair. */
    const uint8_t stray = UART_AF_MAGIC0;
    uint8_t payload[8];
    fill_pattern(payload, sizeof(payload), 0x99);
    uint8_t frame[UART_AF_HEADER_BYTES + sizeof(payload)];
    size_t n = build_frame(frame, UART_AF_TYPE_DATA, 9, payload, sizeof(payload), 0);

    uart_af_feed(&s_parser, &stray, 1, capture_cb, NULL);
    uart_af_feed(&s_parser, frame, n, capture_cb, NULL);

    TEST_ASSERT_EQUAL_INT(2, s_event_count);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_ERR_DESYNC, s_events[0].type);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_FRAME_DATA, s_events[1].type);
    TEST_ASSERT_EQUAL_UINT8(9, s_events[1].seq);
}

void test_magic_inside_payload_no_resync(void)
{
    uint8_t payload[16] = { 0 };
    payload[4] = UART_AF_MAGIC0;
    payload[5] = UART_AF_MAGIC1;
    payload[10] = UART_AF_MAGIC0;
    payload[11] = UART_AF_MAGIC1;
    uint8_t f1[UART_AF_HEADER_BYTES + sizeof(payload)];
    uint8_t f2[UART_AF_HEADER_BYTES + sizeof(payload)];
    size_t n1 = build_frame(f1, UART_AF_TYPE_DATA, 0, payload, sizeof(payload), 0);
    size_t n2 = build_frame(f2, UART_AF_TYPE_DATA, 1, payload, sizeof(payload), 0);

    uart_af_feed(&s_parser, f1, n1, capture_cb, NULL);
    uart_af_feed(&s_parser, f2, n2, capture_cb, NULL);

    TEST_ASSERT_EQUAL_INT(2, s_event_count);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_FRAME_DATA, s_events[0].type);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_FRAME_DATA, s_events[1].type);
    TEST_ASSERT_EQUAL_UINT32(0, s_parser.stats.desync_bytes);
}

/* ── bad header fields ──────────────────────────────────────────────────── */

void test_oversize_len_rejected_then_recovery(void)
{
    /* Hand-build a header with len = 2052 (> max) and any CRC. */
    uint8_t bad[UART_AF_HEADER_BYTES] = {
        UART_AF_MAGIC0, UART_AF_MAGIC1, UART_AF_TYPE_DATA, 0,
        (uint8_t)(2052 & 0xFF), (uint8_t)(2052 >> 8), 0x00, 0x00
    };
    uint8_t payload[8];
    fill_pattern(payload, sizeof(payload), 0x21);
    uint8_t frame[UART_AF_HEADER_BYTES + sizeof(payload)];
    size_t n = build_frame(frame, UART_AF_TYPE_DATA, 0, payload, sizeof(payload), 0);

    uart_af_feed(&s_parser, bad, sizeof(bad), capture_cb, NULL);
    uart_af_feed(&s_parser, frame, n, capture_cb, NULL);

    TEST_ASSERT_TRUE(s_event_count >= 2);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_ERR_DESYNC, s_events[0].type);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_FRAME_DATA, s_events[s_event_count - 1].type);
    TEST_ASSERT_EQUAL_MEMORY(payload, s_events[s_event_count - 1].payload, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT32(0, s_parser.stats.frames_lost); /* first DATA frame seeds seq */
}

void test_unaligned_len_rejected(void)
{
    /* len = 10 (not a multiple of 4) */
    uint8_t bad[UART_AF_HEADER_BYTES] = {
        UART_AF_MAGIC0, UART_AF_MAGIC1, UART_AF_TYPE_DATA, 0,
        10, 0, 0x00, 0x00
    };
    uart_af_feed(&s_parser, bad, sizeof(bad), capture_cb, NULL);

    TEST_ASSERT_EQUAL_UINT32(0, s_parser.stats.frames_data);
    TEST_ASSERT_TRUE(s_event_count >= 1);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_ERR_DESYNC, s_events[0].type);
}

void test_unknown_type_rejected(void)
{
    uint8_t bad[UART_AF_HEADER_BYTES] = {
        UART_AF_MAGIC0, UART_AF_MAGIC1, 0x7F, 0, 0, 0, 0x00, 0x00
    };
    uart_af_feed(&s_parser, bad, sizeof(bad), capture_cb, NULL);

    TEST_ASSERT_EQUAL_UINT32(0, s_parser.stats.frames_data);
    TEST_ASSERT_EQUAL_UINT32(0, s_parser.stats.frames_stop);
    TEST_ASSERT_TRUE(s_event_count >= 1);
    TEST_ASSERT_EQUAL_INT(UART_AF_EVT_ERR_DESYNC, s_events[0].type);
}

/* ── sequence tracking ──────────────────────────────────────────────────── */

static void feed_data_frame_with_seq(uint8_t seq)
{
    uint8_t payload[4] = { 1, 2, 3, 4 };
    uint8_t frame[UART_AF_HEADER_BYTES + 4];
    size_t n = build_frame(frame, UART_AF_TYPE_DATA, seq, payload, 4, 0);
    uart_af_feed(&s_parser, frame, n, capture_cb, NULL);
}

void test_seq_wrap_255_to_0_no_loss(void)
{
    feed_data_frame_with_seq(254);
    feed_data_frame_with_seq(255);
    feed_data_frame_with_seq(0);
    feed_data_frame_with_seq(1);

    TEST_ASSERT_EQUAL_INT(4, s_event_count);
    TEST_ASSERT_EQUAL_UINT32(0, s_parser.stats.frames_lost);
}

void test_seq_gap_counts_lost_frames(void)
{
    feed_data_frame_with_seq(10);
    feed_data_frame_with_seq(14); /* 11, 12, 13 missing */

    TEST_ASSERT_EQUAL_UINT32(3, s_parser.stats.frames_lost);
}

void test_seq_gap_across_wrap(void)
{
    feed_data_frame_with_seq(254);
    feed_data_frame_with_seq(1); /* 255, 0 missing */

    TEST_ASSERT_EQUAL_UINT32(2, s_parser.stats.frames_lost);
}

void test_init_resets_state_and_stats(void)
{
    feed_data_frame_with_seq(10);
    feed_data_frame_with_seq(14);
    TEST_ASSERT_EQUAL_UINT32(3, s_parser.stats.frames_lost);

    uart_af_init(&s_parser);
    TEST_ASSERT_EQUAL_UINT32(0, s_parser.stats.frames_lost);
    TEST_ASSERT_EQUAL_UINT32(0, s_parser.stats.frames_data);

    s_event_count = 0;
    feed_data_frame_with_seq(200); /* fresh seq baseline, no phantom loss */
    TEST_ASSERT_EQUAL_INT(1, s_event_count);
    TEST_ASSERT_EQUAL_UINT32(0, s_parser.stats.frames_lost);
}

/* ── runner ─────────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc16_ccitt_false_check_vector);
    RUN_TEST(test_crc16_empty_is_init_value);
    RUN_TEST(test_single_data_frame_one_feed);
    RUN_TEST(test_max_payload_frame);
    RUN_TEST(test_byte_at_a_time_feed);
    RUN_TEST(test_split_feed_across_payload_boundary);
    RUN_TEST(test_back_to_back_frames_single_feed);
    RUN_TEST(test_stop_frame);
    RUN_TEST(test_stop_frame_with_nonzero_len_rejected);
    RUN_TEST(test_crc_corruption_then_recovery);
    RUN_TEST(test_corrupt_payload_byte_gives_crc_error);
    RUN_TEST(test_garbage_prefix_then_frame);
    RUN_TEST(test_lone_magic0_prefix_then_frame);
    RUN_TEST(test_magic_inside_payload_no_resync);
    RUN_TEST(test_oversize_len_rejected_then_recovery);
    RUN_TEST(test_unaligned_len_rejected);
    RUN_TEST(test_unknown_type_rejected);
    RUN_TEST(test_seq_wrap_255_to_0_no_loss);
    RUN_TEST(test_seq_gap_across_wrap);
    RUN_TEST(test_seq_gap_counts_lost_frames);
    RUN_TEST(test_init_resets_state_and_stats);
    return UNITY_END();
}
