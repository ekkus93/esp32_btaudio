/*
 * test_radio_ring.c — I2S-2 (docs/UNIT_TESTS2_TODO.md): the two SPSC byte
 * rings in radio_ring.c (compressed-network ring: ring_write/radio_read;
 * decoded-PCM ring: pcm_write/radio_pcm_read). Pure logic, no ESP_PLATFORM
 * gate — this test allocates its own backing buffers directly (normally
 * PSRAM-allocated by radio.c's init) and drives the mutex-guarded ring ops.
 */
#include "unity.h"
#include "radio_internal.h"

#include <stdint.h>
#include <string.h>

#define RING_CAP 16
#define PCM_CAP  32 /* must be a multiple of 4 (stereo s16 frame size) */

static uint8_t s_ring_backing[RING_CAP];
static uint8_t s_pcm_backing[PCM_CAP];

void setUp(void)
{
    memset(s_ring_backing, 0, sizeof(s_ring_backing));
    g_radio_ring = s_ring_backing;
    g_radio_ring_cap = RING_CAP;
    g_radio_ring_head = 0;
    g_radio_ring_tail = 0;
    g_radio_ring_count = 0;
    g_radio_ring_mtx = xSemaphoreCreateMutex();

    memset(s_pcm_backing, 0, sizeof(s_pcm_backing));
    g_radio_pcm = s_pcm_backing;
    g_radio_pcm_cap = PCM_CAP;
    g_radio_pcm_head = 0;
    g_radio_pcm_tail = 0;
    g_radio_pcm_count = 0;
    g_radio_pcm_mtx = xSemaphoreCreateMutex();
    g_radio_prebuffered = false;
    atomic_store(&g_radio_prebuffer_bytes, (size_t)8); /* small threshold for tests */
}

void tearDown(void) {}

static void assert_count_invariant_ring(void)
{
    size_t expected = (g_radio_ring_head + RING_CAP - g_radio_ring_tail) % RING_CAP;
    /* When head==tail after a full-capacity fill, the mod arithmetic above
     * would read as "empty" (0) even though the ring can be full; radio_ring.c
     * tracks count separately for exactly this reason, so only assert the
     * invariant when the ring isn't at capacity (the ambiguous head==tail
     * case is resolved by g_radio_ring_count, not derivable from head/tail). */
    if (g_radio_ring_count != RING_CAP) {
        TEST_ASSERT_EQUAL_UINT(expected, g_radio_ring_count);
    }
}

/* ---- ring_write / radio_read (compressed network ring) ---- */

void test_ring_write_into_empty_ring(void)
{
    uint8_t data[4] = {1, 2, 3, 4};
    size_t w = ring_write(data, 4);
    TEST_ASSERT_EQUAL_size_t(4, w);
    TEST_ASSERT_EQUAL_size_t(4, g_radio_ring_count);
}

void test_ring_write_exactly_fills_capacity(void)
{
    uint8_t data[RING_CAP];
    for (int i = 0; i < RING_CAP; i++) data[i] = (uint8_t)i;
    size_t w = ring_write(data, RING_CAP);
    TEST_ASSERT_EQUAL_size_t(RING_CAP, w);
    TEST_ASSERT_EQUAL_size_t(RING_CAP, g_radio_ring_count);
}

void test_ring_write_against_full_ring_is_partial_not_rejected(void)
{
    uint8_t fill[RING_CAP];
    memset(fill, 0xAA, sizeof(fill));
    TEST_ASSERT_EQUAL_size_t(RING_CAP, ring_write(fill, RING_CAP));

    uint8_t more[4] = {9, 9, 9, 9};
    size_t w = ring_write(more, 4);
    TEST_ASSERT_EQUAL_size_t(0, w); /* zero free space -> zero bytes accepted, not an error */
    TEST_ASSERT_EQUAL_size_t(RING_CAP, g_radio_ring_count);
}

void test_ring_write_partial_when_only_some_space_free(void)
{
    uint8_t fill[RING_CAP - 3];
    memset(fill, 0x11, sizeof(fill));
    TEST_ASSERT_EQUAL_size_t(RING_CAP - 3, ring_write(fill, RING_CAP - 3));

    uint8_t more[10];
    memset(more, 0x22, sizeof(more));
    size_t w = ring_write(more, 10); /* only 3 bytes free */
    TEST_ASSERT_EQUAL_size_t(3, w);
    TEST_ASSERT_EQUAL_size_t(RING_CAP, g_radio_ring_count);
}

void test_ring_write_wraparound(void)
{
    uint8_t a[RING_CAP - 2];
    memset(a, 0x01, sizeof(a));
    TEST_ASSERT_EQUAL_size_t(RING_CAP - 2, ring_write(a, sizeof(a)));
    uint8_t drained[RING_CAP - 2];
    TEST_ASSERT_EQUAL_size_t(RING_CAP - 2, radio_read(drained, sizeof(drained)));
    TEST_ASSERT_EQUAL_size_t(0, g_radio_ring_count);
    /* head is now at RING_CAP-2; the next write of 6 bytes must wrap. */
    uint8_t b[6] = {10, 11, 12, 13, 14, 15};
    TEST_ASSERT_EQUAL_size_t(6, ring_write(b, 6));
    TEST_ASSERT_EQUAL_size_t(6, g_radio_ring_count);
    uint8_t out[6];
    TEST_ASSERT_EQUAL_size_t(6, radio_read(out, 6));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b, out, 6);
}

void test_radio_read_less_than_available(void)
{
    uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    ring_write(data, 8);
    uint8_t out[3];
    size_t r = radio_read(out, 3);
    TEST_ASSERT_EQUAL_size_t(3, r);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out, 3);
    TEST_ASSERT_EQUAL_size_t(5, g_radio_ring_count);
}

void test_radio_read_exactly_available(void)
{
    uint8_t data[5] = {1, 2, 3, 4, 5};
    ring_write(data, 5);
    uint8_t out[5];
    TEST_ASSERT_EQUAL_size_t(5, radio_read(out, 5));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out, 5);
    TEST_ASSERT_EQUAL_size_t(0, g_radio_ring_count);
}

void test_radio_read_empty_ring_returns_zero_immediately(void)
{
    uint8_t out[4];
    TEST_ASSERT_EQUAL_size_t(0, radio_read(out, 4));
}

void test_radio_read_null_dst_returns_zero(void)
{
    TEST_ASSERT_EQUAL_size_t(0, radio_read(NULL, 4));
}

void test_radio_read_wraparound_spanning_buffer_end(void)
{
    uint8_t a[RING_CAP - 3];
    memset(a, 0x55, sizeof(a));
    ring_write(a, sizeof(a));
    uint8_t drain[RING_CAP - 3];
    radio_read(drain, sizeof(drain)); /* tail now at RING_CAP-3 */

    /* Write 6 bytes: 3 land at [13,14,15], 3 wrap to [0,1,2]. */
    uint8_t b[6] = {21, 22, 23, 24, 25, 26};
    TEST_ASSERT_EQUAL_size_t(6, ring_write(b, 6));

    uint8_t out[6];
    size_t r = radio_read(out, 6);
    TEST_ASSERT_EQUAL_size_t(6, r);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b, out, 6);
}

void test_ring_byte_exact_round_trip_nonzero_pattern(void)
{
    /* Non-zero, non-uniform pattern so an off-by-one copy can't hide behind
     * an all-zero or all-same buffer. */
    uint8_t data[10] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0xFF, 0x80, 0x7F};
    TEST_ASSERT_EQUAL_size_t(10, ring_write(data, 10));
    uint8_t out[10];
    TEST_ASSERT_EQUAL_size_t(10, radio_read(out, 10));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out, 10);
}

void test_ring_interleaved_write_read_count_invariant(void)
{
    uint8_t chunk[3] = {1, 2, 3};
    uint8_t out[3];
    for (int i = 0; i < 20; i++) {
        ring_write(chunk, 3);
        assert_count_invariant_ring();
        radio_read(out, 2);
        assert_count_invariant_ring();
    }
}

/* ---- pcm_write / radio_pcm_read (decoded-PCM ring) ---- */

void test_pcm_write_into_empty_ring(void)
{
    uint8_t data[8];
    memset(data, 0x01, sizeof(data));
    TEST_ASSERT_EQUAL_size_t(8, pcm_write(data, 8));
    TEST_ASSERT_EQUAL_size_t(8, g_radio_pcm_count);
}

void test_pcm_write_partial_when_full(void)
{
    uint8_t fill[PCM_CAP];
    memset(fill, 0x02, sizeof(fill));
    TEST_ASSERT_EQUAL_size_t(PCM_CAP, pcm_write(fill, PCM_CAP));

    uint8_t more[4] = {9, 9, 9, 9};
    TEST_ASSERT_EQUAL_size_t(0, pcm_write(more, 4));
}

void test_radio_pcm_read_masks_to_whole_frames(void)
{
    uint8_t data[10]; /* not a multiple of 4 */
    memset(data, 0x03, sizeof(data));
    pcm_write(data, 10);
    TEST_ASSERT_EQUAL_size_t(10, g_radio_pcm_count);

    int16_t out[8];
    /* Ask for 3 frames (12 bytes) but only 10 are available -> r = min(12,10)=10,
     * masked to whole frames: 10 & ~3 = 8 bytes = 2 frames. */
    size_t frames = radio_pcm_read(out, 3);
    TEST_ASSERT_EQUAL_size_t(2, frames);
    TEST_ASSERT_EQUAL_size_t(2, g_radio_pcm_count); /* 10 - 8 = 2 bytes left over */
}

void test_radio_pcm_read_null_dst_returns_zero(void)
{
    TEST_ASSERT_EQUAL_size_t(0, radio_pcm_read(NULL, 4));
}

void test_pcm_wraparound_round_trip(void)
{
    uint8_t a[PCM_CAP - 4];
    memset(a, 0x44, sizeof(a));
    pcm_write(a, sizeof(a));
    int16_t drain[(PCM_CAP - 4) / 2];
    radio_pcm_read(drain, (PCM_CAP - 4) / 4); /* drain all frames */
    TEST_ASSERT_EQUAL_size_t(0, g_radio_pcm_count);

    uint8_t b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_EQUAL_size_t(8, pcm_write(b, 8));
    int16_t out[4];
    TEST_ASSERT_EQUAL_size_t(2, radio_pcm_read(out, 2));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b, (uint8_t *)out, 8);
}

/* ---- prebuffer gate transitions ---- */

void test_prebuffer_sets_true_once_threshold_reached(void)
{
    /* threshold set to 8 in setUp() */
    uint8_t data[4];
    memset(data, 0x05, sizeof(data));
    pcm_write(data, 4);
    TEST_ASSERT_FALSE(g_radio_prebuffered);
    pcm_write(data, 4); /* now at 8 bytes: threshold reached */
    TEST_ASSERT_TRUE(g_radio_prebuffered);
}

void test_prebuffer_clears_when_fully_drained(void)
{
    uint8_t data[8];
    memset(data, 0x06, sizeof(data));
    pcm_write(data, 8);
    TEST_ASSERT_TRUE(g_radio_prebuffered);

    int16_t out[4];
    radio_pcm_read(out, 2); /* drains all 8 bytes */
    TEST_ASSERT_EQUAL_size_t(0, g_radio_pcm_count);
    TEST_ASSERT_FALSE(g_radio_prebuffered);
}

void test_prebuffer_stays_true_on_partial_drain(void)
{
    uint8_t data[8];
    memset(data, 0x07, sizeof(data));
    pcm_write(data, 8);
    TEST_ASSERT_TRUE(g_radio_prebuffered);

    int16_t out[2];
    radio_pcm_read(out, 1); /* drains 4 of 8 bytes -- not fully drained */
    TEST_ASSERT_EQUAL_size_t(4, g_radio_pcm_count);
    TEST_ASSERT_TRUE(g_radio_prebuffered);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ring_write_into_empty_ring);
    RUN_TEST(test_ring_write_exactly_fills_capacity);
    RUN_TEST(test_ring_write_against_full_ring_is_partial_not_rejected);
    RUN_TEST(test_ring_write_partial_when_only_some_space_free);
    RUN_TEST(test_ring_write_wraparound);
    RUN_TEST(test_radio_read_less_than_available);
    RUN_TEST(test_radio_read_exactly_available);
    RUN_TEST(test_radio_read_empty_ring_returns_zero_immediately);
    RUN_TEST(test_radio_read_null_dst_returns_zero);
    RUN_TEST(test_radio_read_wraparound_spanning_buffer_end);
    RUN_TEST(test_ring_byte_exact_round_trip_nonzero_pattern);
    RUN_TEST(test_ring_interleaved_write_read_count_invariant);

    RUN_TEST(test_pcm_write_into_empty_ring);
    RUN_TEST(test_pcm_write_partial_when_full);
    RUN_TEST(test_radio_pcm_read_masks_to_whole_frames);
    RUN_TEST(test_radio_pcm_read_null_dst_returns_zero);
    RUN_TEST(test_pcm_wraparound_round_trip);

    RUN_TEST(test_prebuffer_sets_true_once_threshold_reached);
    RUN_TEST(test_prebuffer_clears_when_fully_drained);
    RUN_TEST(test_prebuffer_stays_true_on_partial_drain);
    return UNITY_END();
}
