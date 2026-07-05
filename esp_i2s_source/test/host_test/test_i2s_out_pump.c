/*
 * test_i2s_out_pump — pure pump logic (SIG-1b): drain, zero-fill on underrun,
 * stats accounting, data integrity — verified with a mock sink.
 */
#include "unity.h"
#include "i2s_out.h"
#include "pcm_ring.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Mock sink: captures the most recent block and counts calls/failures. */
typedef struct {
    uint8_t last[256];
    size_t  last_len;
    int     calls;
    int     fail_next;   /* if nonzero, next call returns -1 */
} mock_sink_t;

static int mock_sink(void *ctx, const uint8_t *data, size_t len)
{
    mock_sink_t *m = (mock_sink_t *)ctx;
    m->calls++;
    if (m->fail_next) { m->fail_next = 0; return -1; }
    TEST_ASSERT_TRUE(len <= sizeof(m->last));
    memcpy(m->last, data, len);
    m->last_len = len;
    return 0;
}

static void test_full_block_no_underrun(void)
{
    pcm_ring_t *r = pcm_ring_create(256, false);
    uint8_t in[64];
    for (int i = 0; i < 64; i++) in[i] = (uint8_t)(i + 1);
    pcm_ring_write(r, in, 64);

    mock_sink_t m = {0};
    i2s_out_stats_t st = {0};
    size_t got = i2s_out_pump_once(r, (uint8_t[64]){0}, 64, mock_sink, &m, &st);

    TEST_ASSERT_EQUAL_UINT(64, got);
    TEST_ASSERT_EQUAL_UINT(64, m.last_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, m.last, 64);
    TEST_ASSERT_EQUAL_UINT64(64, st.bytes_written);
    TEST_ASSERT_EQUAL_UINT64(0, st.underrun_bytes);
    TEST_ASSERT_EQUAL_UINT32(0, st.underrun_events);
    pcm_ring_destroy(r);
}

static void test_empty_ring_full_underrun(void)
{
    pcm_ring_t *r = pcm_ring_create(256, false);
    mock_sink_t m = {0};
    i2s_out_stats_t st = {0};
    uint8_t scratch[64];
    memset(scratch, 0xAB, sizeof(scratch));  /* poison to prove zero-fill */

    size_t got = i2s_out_pump_once(r, scratch, 64, mock_sink, &m, &st);

    TEST_ASSERT_EQUAL_UINT(0, got);
    TEST_ASSERT_EQUAL_UINT(64, m.last_len);
    for (int i = 0; i < 64; i++) TEST_ASSERT_EQUAL_UINT8(0, m.last[i]);
    TEST_ASSERT_EQUAL_UINT64(64, st.underrun_bytes);
    TEST_ASSERT_EQUAL_UINT32(1, st.underrun_events);
    TEST_ASSERT_EQUAL_UINT64(64, st.bytes_written);  /* still delivers a block */
    pcm_ring_destroy(r);
}

static void test_partial_zero_fill(void)
{
    pcm_ring_t *r = pcm_ring_create(256, false);
    uint8_t in[20];
    for (int i = 0; i < 20; i++) in[i] = (uint8_t)(0x40 + i);
    pcm_ring_write(r, in, 20);

    mock_sink_t m = {0};
    i2s_out_stats_t st = {0};
    uint8_t scratch[64];
    memset(scratch, 0xCD, sizeof(scratch));

    size_t got = i2s_out_pump_once(r, scratch, 64, mock_sink, &m, &st);

    TEST_ASSERT_EQUAL_UINT(20, got);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, m.last, 20);          /* real data first */
    for (int i = 20; i < 64; i++) TEST_ASSERT_EQUAL_UINT8(0, m.last[i]);  /* filled */
    TEST_ASSERT_EQUAL_UINT64(44, st.underrun_bytes);
    TEST_ASSERT_EQUAL_UINT32(1, st.underrun_events);
    pcm_ring_destroy(r);
}

static void test_stats_accumulate_over_pumps(void)
{
    pcm_ring_t *r = pcm_ring_create(256, false);
    uint8_t in[32] = {0};
    mock_sink_t m = {0};
    i2s_out_stats_t st = {0};
    uint8_t scratch[32];

    pcm_ring_write(r, in, 32);
    i2s_out_pump_once(r, scratch, 32, mock_sink, &m, &st);  /* full */
    i2s_out_pump_once(r, scratch, 32, mock_sink, &m, &st);  /* underrun */
    i2s_out_pump_once(r, scratch, 32, mock_sink, &m, &st);  /* underrun */

    TEST_ASSERT_EQUAL_UINT64(96, st.bytes_written);
    TEST_ASSERT_EQUAL_UINT64(64, st.underrun_bytes);
    TEST_ASSERT_EQUAL_UINT32(2, st.underrun_events);
    TEST_ASSERT_EQUAL_INT(3, m.calls);
    pcm_ring_destroy(r);
}

static void test_sink_failure_not_counted_as_written(void)
{
    pcm_ring_t *r = pcm_ring_create(256, false);
    uint8_t in[16] = {0};
    pcm_ring_write(r, in, 16);
    mock_sink_t m = {0};
    m.fail_next = 1;
    i2s_out_stats_t st = {0};
    uint8_t scratch[16];

    i2s_out_pump_once(r, scratch, 16, mock_sink, &m, &st);
    TEST_ASSERT_EQUAL_UINT64(0, st.bytes_written);  /* sink failed */
    pcm_ring_destroy(r);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_full_block_no_underrun);
    RUN_TEST(test_empty_ring_full_underrun);
    RUN_TEST(test_partial_zero_fill);
    RUN_TEST(test_stats_accumulate_over_pumps);
    RUN_TEST(test_sink_failure_not_counted_as_written);
    return UNITY_END();
}
