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
    pcm_ring_t *r = pcm_ring_create(100, false);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_UINT(100, pcm_ring_capacity(r));
    TEST_ASSERT_EQUAL_UINT(0, pcm_ring_used(r));
    TEST_ASSERT_EQUAL_UINT(100, pcm_ring_free(r));
    pcm_ring_destroy(r);
}

static void test_write_then_read_fifo(void)
{
    pcm_ring_t *r = pcm_ring_create(64, false);
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
    pcm_ring_t *r = pcm_ring_create(8, false);
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
    pcm_ring_t *r = pcm_ring_create(32, false);
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
    pcm_ring_t *r = pcm_ring_create(10, false);
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
    pcm_ring_t *r = pcm_ring_create(16, false);
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
    pcm_ring_t *r = pcm_ring_create(100, false);
    uint8_t buf[60] = {0};
    pcm_ring_write(r, buf, 60);
    pcm_ring_read(r, buf, 40);
    pcm_ring_write(r, buf, 10);   /* used now 30, but peak was 60 */
    TEST_ASSERT_EQUAL_UINT(60, pcm_ring_peak_used(r));
    pcm_ring_destroy(r);
}

static void test_reset_empties(void)
{
    pcm_ring_t *r = pcm_ring_create(32, false);
    uint8_t buf[20] = {0};
    pcm_ring_write(r, buf, 20);
    pcm_ring_reset(r);
    TEST_ASSERT_EQUAL_UINT(0, pcm_ring_used(r));
    TEST_ASSERT_EQUAL_UINT(32, pcm_ring_free(r));
    TEST_ASSERT_EQUAL_UINT(0, pcm_ring_peak_used(r));
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
    return UNITY_END();
}
