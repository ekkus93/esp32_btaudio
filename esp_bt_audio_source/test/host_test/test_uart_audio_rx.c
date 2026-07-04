/**
 * test_uart_audio_rx.c — RX pump: frames -> staging ring, STOP, CRC abort,
 * feedback line formatting. Uses the REAL parser and uart_source.
 */

#include <string.h>
#include <stdio.h>
#include "unity.h"
#include "uart_audio_rx.h"

static uart_audio_rx_t s_rx;

void setUp(void)
{
    uart_source_stop();
    uart_audio_rx_init(&s_rx);
}

void tearDown(void)
{
    uart_source_stop();
}

/* frame builder (same wire format as test_uart_audio_frame.c) */
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

static void feed_data_frame(uint8_t seq, uint16_t len, uint16_t crc_delta)
{
    static uint8_t payload[256];
    for (uint16_t i = 0; i < len; i++) {
        payload[i] = (uint8_t)i;
    }
    uint8_t frame[UART_AF_HEADER_BYTES + 256];
    size_t n = build_frame(frame, UART_AF_TYPE_DATA, seq, payload, len, crc_delta);
    uart_audio_rx_feed(&s_rx, frame, n);
}

/* ── frames land in the staging ring ────────────────────────────────────── */

void test_data_frames_reach_uart_source(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(1024));

    feed_data_frame(0, 128, 0);
    feed_data_frame(1, 128, 0);

    uart_source_stats_t st;
    uart_source_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT32(256, st.bytes_in);
    TEST_ASSERT_EQUAL_size_t(256, st.ring_used);
    TEST_ASSERT_FALSE(uart_audio_rx_stop_seen(&s_rx));
    TEST_ASSERT_FALSE(uart_audio_rx_crc_abort(&s_rx));
}

/* ── STOP frame ─────────────────────────────────────────────────────────── */

void test_stop_frame_sets_stop_and_requests_drain(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(256));
    feed_data_frame(0, 128, 0); /* 128 >= 50% of 256 -> ACTIVE */
    TEST_ASSERT_EQUAL(UART_SOURCE_STATE_ACTIVE, uart_source_get_state());

    uint8_t stop[UART_AF_HEADER_BYTES];
    size_t n = build_frame(stop, UART_AF_TYPE_STOP, 0, NULL, 0, 0);
    uart_audio_rx_feed(&s_rx, stop, n);

    TEST_ASSERT_TRUE(uart_audio_rx_stop_seen(&s_rx));
    TEST_ASSERT_EQUAL(UART_SOURCE_STATE_DRAINING, uart_source_get_state());
}

/* ── CRC abort ──────────────────────────────────────────────────────────── */

void test_crc_abort_after_more_than_threshold(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(4096));

    for (uint8_t i = 0; i < UART_AUDIO_CRC_ABORT_THRESHOLD; i++) {
        feed_data_frame(i, 64, 1); /* bad CRC */
    }
    TEST_ASSERT_FALSE(uart_audio_rx_crc_abort(&s_rx)); /* exactly 8: not yet */

    feed_data_frame(8, 64, 1); /* 9th consecutive */
    TEST_ASSERT_TRUE(uart_audio_rx_crc_abort(&s_rx));
}

void test_good_frame_resets_crc_run(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(4096));

    for (uint8_t i = 0; i < UART_AUDIO_CRC_ABORT_THRESHOLD; i++) {
        feed_data_frame(i, 64, 1); /* 8 bad */
    }
    feed_data_frame(8, 64, 0);     /* good frame resets the run */
    for (uint8_t i = 9; i < 9 + UART_AUDIO_CRC_ABORT_THRESHOLD; i++) {
        feed_data_frame(i, 64, 1); /* 8 more bad */
    }

    TEST_ASSERT_FALSE(uart_audio_rx_crc_abort(&s_rx));
}

/* ── feedback / stats formatting ────────────────────────────────────────── */

void test_fill_line_format(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(1024));
    feed_data_frame(0, 128, 0);
    feed_data_frame(1, 128, 0);
    feed_data_frame(2, 64, 1); /* one CRC error */

    char line[160];
    int n = uart_audio_format_fill_line(line, sizeof(line), &s_rx, 176400);

    TEST_ASSERT_GREATER_THAN(0, n);
    /* UA|FILL|used|cap|und|crc|lost|ovf|seq|a2dp_Bps */
    TEST_ASSERT_EQUAL_STRING("UA|FILL|256|1024|0|1|0|0|1|176400\r\n", line);
}

void test_stopped_data_format(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(1024));
    feed_data_frame(0, 128, 0);
    feed_data_frame(1, 128, 0);
    feed_data_frame(3, 128, 0); /* seq gap: frame 2 lost */

    uart_source_stats_t st;
    uart_source_get_stats(&st);

    char buf[160];
    int n = uart_audio_format_stopped_data(buf, sizeof(buf), &s_rx, &st);

    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING("frames=3,bytes=384,crc=0,und=0,ovf=0,lost=1", buf);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_data_frames_reach_uart_source);
    RUN_TEST(test_stop_frame_sets_stop_and_requests_drain);
    RUN_TEST(test_crc_abort_after_more_than_threshold);
    RUN_TEST(test_good_frame_resets_crc_run);
    RUN_TEST(test_fill_line_format);
    RUN_TEST(test_stopped_data_format);
    return UNITY_END();
}
