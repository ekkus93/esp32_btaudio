/*
 * test_pcm_ring — SPSC ring correctness (SIG-1b): capacity accounting,
 * FIFO integrity, wraparound, full/empty edges, peak tracking, reset.
 */
#include "unity.h"
#include "pcm_ring.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_create_empty_state(void)
{
    pcm_ring_t *r = pcm_ring_create(100, PCM_RING_INTERNAL_ONLY);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_UINT(100, pcm_ring_capacity(r));
    TEST_ASSERT_EQUAL_UINT(0, pcm_ring_used(r));
    TEST_ASSERT_EQUAL_UINT(100, pcm_ring_free(r));
    pcm_ring_destroy(r);
}

static void test_write_then_read_fifo(void)
{
    pcm_ring_t *r = pcm_ring_create(64, PCM_RING_INTERNAL_ONLY);
    uint8_t in[10], out[10];
    for (int i = 0; i < 10; i++) in[i] = (uint8_t)(i + 1);

    TEST_ASSERT_EQUAL_UINT(10, pcm_ring_write(r, in, 10));
    TEST_ASSERT_EQUAL_UINT(10, pcm_ring_used(r));
    TEST_ASSERT_EQUAL_UINT(54, pcm_ring_free(r));

    TEST_ASSERT_EQUAL_UINT(10, pcm_ring_read(r, out, 10));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, 10);
    TEST_ASSERT_EQUAL_UINT(0, pcm_ring_used(r));
    pcm_ring_destroy(r);
}

static void test_write_caps_at_capacity(void)
{
    pcm_ring_t *r = pcm_ring_create(8, PCM_RING_INTERNAL_ONLY);
    uint8_t in[16];
    for (int i = 0; i < 16; i++) in[i] = (uint8_t)i;
    /* only 8 usable bytes fit */
    TEST_ASSERT_EQUAL_UINT(8, pcm_ring_write(r, in, 16));
    TEST_ASSERT_EQUAL_UINT(8, pcm_ring_used(r));
    TEST_ASSERT_EQUAL_UINT(0, pcm_ring_free(r));
    /* further write rejected */
    TEST_ASSERT_EQUAL_UINT(0, pcm_ring_write(r, in, 4));
    pcm_ring_destroy(r);
}

static void test_read_caps_at_used(void)
{
    pcm_ring_t *r = pcm_ring_create(32, PCM_RING_INTERNAL_ONLY);
    uint8_t in[5] = {9, 8, 7, 6, 5};
    uint8_t out[16];
    pcm_ring_write(r, in, 5);
    TEST_ASSERT_EQUAL_UINT(5, pcm_ring_read(r, out, 16));  /* only 5 available */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, 5);
    TEST_ASSERT_EQUAL_UINT(0, pcm_ring_read(r, out, 16));  /* now empty */
    pcm_ring_destroy(r);
}

/* Drive the head/tail past the buffer end to exercise the wrap memcpy. */
static void test_wraparound_integrity(void)
{
    pcm_ring_t *r = pcm_ring_create(10, PCM_RING_INTERNAL_ONLY);
    uint8_t chunk[6], out[6];
    uint8_t counter = 0;

    for (int round = 0; round < 20; round++) {
        for (int i = 0; i < 6; i++) chunk[i] = counter++;
        TEST_ASSERT_EQUAL_UINT(6, pcm_ring_write(r, chunk, 6));
        TEST_ASSERT_EQUAL_UINT(6, pcm_ring_read(r, out, 6));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(chunk, out, 6);
    }
    TEST_ASSERT_EQUAL_UINT(0, pcm_ring_used(r));
    pcm_ring_destroy(r);
}

static void test_partial_interleaved(void)
{
    pcm_ring_t *r = pcm_ring_create(16, PCM_RING_INTERNAL_ONLY);
    uint8_t a[12], b[8], out[20];
    for (int i = 0; i < 12; i++) a[i] = (uint8_t)(0x10 + i);
    for (int i = 0; i < 8; i++)  b[i] = (uint8_t)(0xA0 + i);

    TEST_ASSERT_EQUAL_UINT(12, pcm_ring_write(r, a, 12));
    TEST_ASSERT_EQUAL_UINT(6, pcm_ring_read(r, out, 6));       /* drain 6 */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a, out, 6);
    TEST_ASSERT_EQUAL_UINT(8, pcm_ring_write(r, b, 8));        /* 6 free + wrap */
    /* remaining: a[6..11] then b[0..7] */
    TEST_ASSERT_EQUAL_UINT(14, pcm_ring_read(r, out, 20));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a + 6, out, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b, out + 6, 8);
    pcm_ring_destroy(r);
}

static void test_peak_used_tracks_max(void)
{
    pcm_ring_t *r = pcm_ring_create(100, PCM_RING_INTERNAL_ONLY);
    uint8_t buf[60] = {0};
    pcm_ring_write(r, buf, 60);
    pcm_ring_read(r, buf, 40);
    pcm_ring_write(r, buf, 10);   /* used now 30, but peak was 60 */
    TEST_ASSERT_EQUAL_UINT(60, pcm_ring_peak_used(r));
    pcm_ring_destroy(r);
}

static void test_reset_empties(void)
{
    pcm_ring_t *r = pcm_ring_create(32, PCM_RING_INTERNAL_ONLY);
    uint8_t buf[20] = {0};
    pcm_ring_write(r, buf, 20);
    pcm_ring_reset(r);
    TEST_ASSERT_EQUAL_UINT(0, pcm_ring_used(r));
    TEST_ASSERT_EQUAL_UINT(32, pcm_ring_free(r));
    TEST_ASSERT_EQUAL_UINT(0, pcm_ring_peak_used(r));
    pcm_ring_destroy(r);
}

/* TODO 3.2: overflow/degenerate-capacity guards. */
static void test_create_rejects_zero_capacity(void)
{
    TEST_ASSERT_NULL(pcm_ring_create(0, PCM_RING_INTERNAL_ONLY));
}

static void test_create_rejects_size_max_capacity(void)
{
    /* capacity + 1 (the wasted full/empty slot) must not wrap to 0. */
    TEST_ASSERT_NULL(pcm_ring_create(SIZE_MAX, PCM_RING_INTERNAL_ONLY));
}

/* TODO 3.3: peek/consume. */
static void test_peek_does_not_change_used_count(void)
{
    pcm_ring_t *r = pcm_ring_create(32, PCM_RING_INTERNAL_ONLY);
    uint8_t in[10], out[10] = {0};
    for (int i = 0; i < 10; i++) in[i] = (uint8_t)(i + 1);
    pcm_ring_write(r, in, 10);

    TEST_ASSERT_EQUAL_UINT(10, pcm_ring_peek(r, out, 10));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, 10);
    TEST_ASSERT_EQUAL_UINT(10, pcm_ring_used(r));  /* unchanged */

    /* Peeking again returns the SAME bytes — tail never moved. */
    uint8_t out2[10] = {0};
    TEST_ASSERT_EQUAL_UINT(10, pcm_ring_peek(r, out2, 10));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out2, 10);
    pcm_ring_destroy(r);
}

static void test_consume_changes_used_count(void)
{
    pcm_ring_t *r = pcm_ring_create(32, PCM_RING_INTERNAL_ONLY);
    uint8_t in[10];
    for (int i = 0; i < 10; i++) in[i] = (uint8_t)(i + 1);
    pcm_ring_write(r, in, 10);

    TEST_ASSERT_EQUAL_UINT(4, pcm_ring_consume(r, 4));
    TEST_ASSERT_EQUAL_UINT(6, pcm_ring_used(r));

    uint8_t out[6] = {0};
    TEST_ASSERT_EQUAL_UINT(6, pcm_ring_peek(r, out, 6));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in + 4, out, 6);  /* consumed prefix skipped */
    pcm_ring_destroy(r);
}

static void test_producer_cannot_overwrite_peeked_bytes(void)
{
    pcm_ring_t *r = pcm_ring_create(10, PCM_RING_INTERNAL_ONLY);
    uint8_t in[10];
    for (int i = 0; i < 10; i++) in[i] = (uint8_t)(0x30 + i);
    pcm_ring_write(r, in, 10);  /* ring now full */

    uint8_t peeked[10] = {0};
    TEST_ASSERT_EQUAL_UINT(10, pcm_ring_peek(r, peeked, 10));

    /* Ring is full (tail unmoved) — the producer cannot write anything, so
     * the peeked bytes are provably still intact in the ring. */
    uint8_t more[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_UINT(0, pcm_ring_write(r, more, 4));

    uint8_t recheck[10] = {0};
    pcm_ring_peek(r, recheck, 10);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, recheck, 10);
    pcm_ring_destroy(r);
}

static void test_wraparound_peek_and_partial_consume(void)
{
    pcm_ring_t *r = pcm_ring_create(16, PCM_RING_INTERNAL_ONLY);
    uint8_t a[12], b[8];
    for (int i = 0; i < 12; i++) a[i] = (uint8_t)(0x10 + i);
    for (int i = 0; i < 8; i++)  b[i] = (uint8_t)(0xA0 + i);

    pcm_ring_write(r, a, 12);
    pcm_ring_consume(r, 6);              /* drop a[0..5]; tail now mid-buffer */
    pcm_ring_write(r, b, 8);             /* wraps: 6 free (16-12+2? just fits) */

    /* Remaining logical content: a[6..11] then b[0..7] = 14 bytes, spanning
     * the buffer wrap point. */
    uint8_t out[20] = {0};
    TEST_ASSERT_EQUAL_UINT(14, pcm_ring_peek(r, out, 20));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a + 6, out, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b, out + 6, 8);
    TEST_ASSERT_EQUAL_UINT(14, pcm_ring_used(r));  /* peek didn't consume */

    /* Partial consume across the wrap boundary. */
    TEST_ASSERT_EQUAL_UINT(9, pcm_ring_consume(r, 9));  /* a[6..11] + b[0..2] */
    TEST_ASSERT_EQUAL_UINT(5, pcm_ring_used(r));
    uint8_t rest[5] = {0};
    pcm_ring_peek(r, rest, 5);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b + 3, rest, 5);  /* b[3..7] remain */
    pcm_ring_destroy(r);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_create_empty_state);
    RUN_TEST(test_write_then_read_fifo);
    RUN_TEST(test_write_caps_at_capacity);
    RUN_TEST(test_read_caps_at_used);
    RUN_TEST(test_wraparound_integrity);
    RUN_TEST(test_partial_interleaved);
    RUN_TEST(test_peak_used_tracks_max);
    RUN_TEST(test_reset_empties);
    RUN_TEST(test_create_rejects_zero_capacity);
    RUN_TEST(test_create_rejects_size_max_capacity);
    RUN_TEST(test_peek_does_not_change_used_count);
    RUN_TEST(test_consume_changes_used_count);
    RUN_TEST(test_producer_cannot_overwrite_peeked_bytes);
    RUN_TEST(test_wraparound_peek_and_partial_consume);
    return UNITY_END();
}
