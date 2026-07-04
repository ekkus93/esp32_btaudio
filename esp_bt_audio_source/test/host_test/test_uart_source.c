/**
 * test_uart_source.c — unit tests for the UART audio source lifecycle
 *
 * Covers:
 *   - start: PREBUFFER state, inactive, stats initialized; double-start rejected
 *   - write-before-start returns 0
 *   - prebuffer -> ACTIVE transition at >= 50% ring fill
 *   - byte-exact 2x upsampled fill output (prev seeded to 0 at start)
 *   - underrun: zero-fill tail + underrun_events, fill still returns dst_bytes
 *   - write-when-full: short write + overflow_events
 *   - drain: stays active until ring empties, then deactivates (no underrun)
 *   - stop: deactivates, frees ring, safe when never started; restart works
 */

#include <string.h>
#include "unity.h"
#include "uart_source.h"

void setUp(void)
{
    uart_source_stop(); /* ensure clean slate; safe when not started */
}

void tearDown(void)
{
    uart_source_stop();
}

/* helper: interleaved stereo ramp frames starting at base, step per frame */
static void make_frames(int16_t *dst, size_t frames, int16_t base, int16_t step)
{
    for (size_t i = 0; i < frames; i++) {
        dst[2 * i] = (int16_t)(base + (int16_t)i * step);
        dst[2 * i + 1] = (int16_t)(-(base + (int16_t)i * step));
    }
}

/* ── start / initial state ──────────────────────────────────────────────── */

void test_start_enters_prebuffer_inactive(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(1024));
    TEST_ASSERT_FALSE(uart_source_is_active());
    TEST_ASSERT_EQUAL(UART_SOURCE_STATE_PREBUFFER, uart_source_get_state());

    uart_source_stats_t st;
    uart_source_get_stats(&st);
    TEST_ASSERT_EQUAL_size_t(1024, st.ring_capacity);
    TEST_ASSERT_EQUAL_size_t(512, st.prebuffer_target);
    TEST_ASSERT_EQUAL_size_t(0, st.ring_used);
    TEST_ASSERT_EQUAL_UINT32(0, st.bytes_in);
    TEST_ASSERT_EQUAL_UINT32(0, st.underrun_events);
}

void test_double_start_rejected(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(1024));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, uart_source_start(1024));
}

void test_start_zero_size_rejected(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, uart_source_start(0));
    TEST_ASSERT_EQUAL(UART_SOURCE_STATE_INACTIVE, uart_source_get_state());
}

void test_write_before_start_returns_zero(void)
{
    uint8_t data[16] = { 0 };
    TEST_ASSERT_EQUAL_size_t(0, uart_source_write(data, sizeof(data)));
}

void test_fill_before_start_returns_zero(void)
{
    uint8_t dst[64];
    TEST_ASSERT_EQUAL_size_t(0, uart_source_fill(dst, sizeof(dst)));
}

/* ── prebuffer activation ───────────────────────────────────────────────── */

void test_prebuffer_activates_at_half_fill(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(1024));

    uint8_t chunk[256] = { 0 };
    TEST_ASSERT_EQUAL_size_t(256, uart_source_write(chunk, 256));
    TEST_ASSERT_EQUAL(UART_SOURCE_STATE_PREBUFFER, uart_source_get_state());
    TEST_ASSERT_FALSE(uart_source_is_active());

    TEST_ASSERT_EQUAL_size_t(256, uart_source_write(chunk, 256)); /* now 512 = 50% */
    TEST_ASSERT_EQUAL(UART_SOURCE_STATE_ACTIVE, uart_source_get_state());
    TEST_ASSERT_TRUE(uart_source_is_active());
}

void test_fill_during_prebuffer_returns_zero(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(1024));
    uint8_t chunk[64] = { 0 };
    uart_source_write(chunk, sizeof(chunk));

    uint8_t dst[64];
    TEST_ASSERT_EQUAL_size_t(0, uart_source_fill(dst, sizeof(dst)));
}

/* ── fill: byte-exact upsampling ────────────────────────────────────────── */

void test_fill_output_is_exact_2x_upsample(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(64));

    /* 8 input frames (32 bytes) activates the source (>= 32 = 50% of 64) */
    int16_t in[16];
    make_frames(in, 8, 100, 100); /* L: 100..800, R: -100..-800 */
    TEST_ASSERT_EQUAL_size_t(32, uart_source_write((const uint8_t *)in, 32));
    TEST_ASSERT_TRUE(uart_source_is_active());

    int16_t out[32]; /* 16 output frames = 64 bytes */
    TEST_ASSERT_EQUAL_size_t(64, uart_source_fill((uint8_t *)out, 64));

    /* prev seeded {0,0}: first midpoint is in[0]/2 */
    TEST_ASSERT_EQUAL_INT16(50, out[0]);
    TEST_ASSERT_EQUAL_INT16(-50, out[1]);
    TEST_ASSERT_EQUAL_INT16(100, out[2]);
    TEST_ASSERT_EQUAL_INT16(-100, out[3]);
    TEST_ASSERT_EQUAL_INT16(150, out[4]); /* midpoint 100 -> 200 */
    TEST_ASSERT_EQUAL_INT16(200, out[6]);
    /* last frame pair: midpoint 700 -> 800, then 800 */
    TEST_ASSERT_EQUAL_INT16(750, out[28]);
    TEST_ASSERT_EQUAL_INT16(800, out[30]);
    TEST_ASSERT_EQUAL_INT16(-800, out[31]);

    uart_source_stats_t st;
    uart_source_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT32(32, st.bytes_in);
    TEST_ASSERT_EQUAL_UINT32(64, st.bytes_out);
    TEST_ASSERT_EQUAL_size_t(0, st.ring_used);
    TEST_ASSERT_EQUAL_UINT32(0, st.underrun_events);
}

void test_fill_prev_carries_across_calls(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(64));

    int16_t in[16];
    make_frames(in, 8, 100, 100);
    uart_source_write((const uint8_t *)in, 32);

    int16_t out_a[16], out_b[16];
    TEST_ASSERT_EQUAL_size_t(32, uart_source_fill((uint8_t *)out_a, 32)); /* frames 1-4 */
    TEST_ASSERT_EQUAL_size_t(32, uart_source_fill((uint8_t *)out_b, 32)); /* frames 5-8 */

    /* out_b starts with midpoint 400 -> 500 = 450: continuity across calls */
    TEST_ASSERT_EQUAL_INT16(450, out_b[0]);
    TEST_ASSERT_EQUAL_INT16(500, out_b[2]);
}

/* ── underrun ───────────────────────────────────────────────────────────── */

void test_underrun_zero_fills_and_counts(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(64));

    int16_t in[16];
    make_frames(in, 8, 1000, 0); /* constant 1000 */
    uart_source_write((const uint8_t *)in, 32);
    TEST_ASSERT_TRUE(uart_source_is_active());

    uint8_t dst[128]; /* wants 64 input bytes; only 32 buffered */
    memset(dst, 0xAA, sizeof(dst));
    TEST_ASSERT_EQUAL_size_t(128, uart_source_fill(dst, 128));

    /* first 64 bytes are real audio, rest must be zero-filled */
    const int16_t *out = (const int16_t *)(const void *)dst;
    TEST_ASSERT_EQUAL_INT16(1000, out[2]); /* real sample survives */
    for (size_t i = 64; i < 128; i++) {
        TEST_ASSERT_EQUAL_UINT8(0, dst[i]);
    }

    uart_source_stats_t st;
    uart_source_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT32(1, st.underrun_events);
    TEST_ASSERT_TRUE(uart_source_is_active()); /* underrun does not deactivate */
}

/* ── overflow ───────────────────────────────────────────────────────────── */

void test_write_when_full_short_writes_and_counts(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(64));

    uint8_t chunk[64] = { 1 };
    TEST_ASSERT_EQUAL_size_t(64, uart_source_write(chunk, 64)); /* exactly full */

    uart_source_stats_t st;
    uart_source_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT32(0, st.overflow_events);

    TEST_ASSERT_EQUAL_size_t(0, uart_source_write(chunk, 16)); /* no room */
    uart_source_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT32(1, st.overflow_events);
}

/* ── drain ──────────────────────────────────────────────────────────────── */

void test_drain_stays_active_until_empty_then_deactivates(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(64));

    int16_t in[16];
    make_frames(in, 8, 500, 0);
    uart_source_write((const uint8_t *)in, 32);
    TEST_ASSERT_TRUE(uart_source_is_active());

    uart_source_request_drain();
    TEST_ASSERT_EQUAL(UART_SOURCE_STATE_DRAINING, uart_source_get_state());
    TEST_ASSERT_TRUE(uart_source_is_active());

    /* consume the 32 buffered bytes exactly -> 64 out; still draining after
     * this call is acceptable only if ring not seen empty; next fill ends it */
    uint8_t dst[64];
    uart_source_fill(dst, 64);

    /* ring now empty: a further fill (or the same one) must deactivate */
    if (uart_source_is_active()) {
        uart_source_fill(dst, 64);
    }
    TEST_ASSERT_FALSE(uart_source_is_active());
    TEST_ASSERT_EQUAL(UART_SOURCE_STATE_INACTIVE, uart_source_get_state());

    /* drain tail is not an underrun */
    uart_source_stats_t st;
    uart_source_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT32(0, st.underrun_events);
}

void test_drain_during_prebuffer_activates_playout(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(1024));

    uint8_t chunk[64] = { 0 };
    uart_source_write(chunk, 64); /* well below 50% */
    TEST_ASSERT_FALSE(uart_source_is_active());

    uart_source_request_drain(); /* short stream: play out what we have */
    TEST_ASSERT_TRUE(uart_source_is_active());
    TEST_ASSERT_EQUAL(UART_SOURCE_STATE_DRAINING, uart_source_get_state());
}

/* ── stop / restart ─────────────────────────────────────────────────────── */

void test_stop_without_start_is_safe(void)
{
    uart_source_stop();
    uart_source_stop();
    TEST_ASSERT_EQUAL(UART_SOURCE_STATE_INACTIVE, uart_source_get_state());
}

void test_stop_deactivates_and_allows_restart(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(64));
    uint8_t chunk[32] = { 0 };
    uart_source_write(chunk, 32);
    TEST_ASSERT_TRUE(uart_source_is_active());

    uart_source_stop();
    TEST_ASSERT_FALSE(uart_source_is_active());
    TEST_ASSERT_EQUAL(UART_SOURCE_STATE_INACTIVE, uart_source_get_state());
    TEST_ASSERT_EQUAL_size_t(0, uart_source_write(chunk, 32));

    /* restart gets a fresh ring, fresh stats, fresh prev seed */
    TEST_ASSERT_EQUAL(ESP_OK, uart_source_start(64));
    uart_source_stats_t st;
    uart_source_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT32(0, st.bytes_in);
    TEST_ASSERT_EQUAL(UART_SOURCE_STATE_PREBUFFER, uart_source_get_state());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_start_enters_prebuffer_inactive);
    RUN_TEST(test_double_start_rejected);
    RUN_TEST(test_start_zero_size_rejected);
    RUN_TEST(test_write_before_start_returns_zero);
    RUN_TEST(test_fill_before_start_returns_zero);
    RUN_TEST(test_prebuffer_activates_at_half_fill);
    RUN_TEST(test_fill_during_prebuffer_returns_zero);
    RUN_TEST(test_fill_output_is_exact_2x_upsample);
    RUN_TEST(test_fill_prev_carries_across_calls);
    RUN_TEST(test_underrun_zero_fills_and_counts);
    RUN_TEST(test_write_when_full_short_writes_and_counts);
    RUN_TEST(test_drain_stays_active_until_empty_then_deactivates);
    RUN_TEST(test_drain_during_prebuffer_activates_playout);
    RUN_TEST(test_stop_without_start_is_safe);
    RUN_TEST(test_stop_deactivates_and_allows_restart);
    return UNITY_END();
}
