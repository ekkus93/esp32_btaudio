/**
 * test_audio_processor_state.c — unit tests for audio_processor_state.c helpers
 *
 * Covers:
 *   - audio_bytes_per_sample()        all three bit depths + invalid value
 *   - audio_get_runtime_work_bytes()  zero-fallback and set-value paths
 *   - bt_manager_is_a2dp_connected()  weak stub default returns false
 */

#include <string.h>
#include "unity.h"
#include "audio_processor_internal.h"

/* Forward declarations for functions that live in audio_processor_state.c */
int    audio_bytes_per_sample(audio_bit_depth_t bit_depth);
size_t audio_get_runtime_work_bytes(void);

/* s_runtime_work_bytes is defined in audio_processor_state.c and exposed via
 * audio_processor_internal.h as an extern so tests can reset it. */
extern size_t s_runtime_work_bytes;

void setUp(void)
{
    s_runtime_work_bytes = 0;  /* reset to uninitialized state before each test */
}

void tearDown(void)
{
    s_runtime_work_bytes = 0;
}

/* ── audio_bytes_per_sample ──────────────────────────────────────────────── */

void test_bytes_per_sample_16bit_returns_2(void)
{
    TEST_ASSERT_EQUAL_INT(2, audio_bytes_per_sample(AUDIO_BIT_DEPTH_16));
}

void test_bytes_per_sample_24bit_returns_4(void)
{
    /* 24-bit audio is stored in 32-bit containers on ESP32 */
    TEST_ASSERT_EQUAL_INT(4, audio_bytes_per_sample(AUDIO_BIT_DEPTH_24));
}

void test_bytes_per_sample_32bit_returns_4(void)
{
    TEST_ASSERT_EQUAL_INT(4, audio_bytes_per_sample(AUDIO_BIT_DEPTH_32));
}

void test_bytes_per_sample_unknown_returns_2(void)
{
    /* Default case must return a safe non-zero value (2 = 16-bit default).
     * This prevents divide-by-zero in callers that use the result as a divisor. */
    int result = audio_bytes_per_sample((audio_bit_depth_t)0xFF);
    TEST_ASSERT_EQUAL_INT(2, result);
    TEST_ASSERT_GREATER_THAN_INT(0, result);
}

/* ── audio_get_runtime_work_bytes ────────────────────────────────────────── */

void test_runtime_work_bytes_uninitialized_returns_default(void)
{
    /* When s_runtime_work_bytes is 0 (never set), the function must return the
     * compile-time default AUDIO_WORK_BUFFER_BYTES rather than 0. */
    s_runtime_work_bytes = 0;
    size_t result = audio_get_runtime_work_bytes();
    TEST_ASSERT_GREATER_THAN(0u, result);
    /* Should equal the compile-time fallback */
    TEST_ASSERT_EQUAL_UINT(AUDIO_WORK_BUFFER_BYTES, result);
}

void test_runtime_work_bytes_set_returns_set_value(void)
{
    s_runtime_work_bytes = 4096;
    size_t result = audio_get_runtime_work_bytes();
    TEST_ASSERT_EQUAL_UINT(4096u, result);
}

void test_work_buffer_dram_min_is_at_least_half_compile_time_default(void)
{
    /* AUDIO_WORK_BUFFER_DRAM_MIN_BYTES is the floor for DRAM-only boards.
     * The allocator halves AUDIO_WORK_BUFFER_BYTES on DRAM-only systems.
     * Guard against accidental reduction of this constant. */
    TEST_ASSERT_EQUAL_UINT(AUDIO_WORK_BUFFER_BYTES / 2U, AUDIO_WORK_BUFFER_DRAM_MIN_BYTES);
    /* Floor must still be large enough to be useful (at least 1 KiB). */
    TEST_ASSERT_GREATER_OR_EQUAL((size_t)1024u, AUDIO_WORK_BUFFER_DRAM_MIN_BYTES);
}

void test_runtime_work_bytes_large_value_returned_unchanged(void)
{
    s_runtime_work_bytes = 65536;
    TEST_ASSERT_EQUAL_UINT(65536u, audio_get_runtime_work_bytes());
}

/* ── bt_manager_is_a2dp_connected weak stub ─────────────────────────────── */

void test_a2dp_connected_weak_stub_returns_false(void)
{
    /* The default weak stub in audio_processor_state.c must return false
     * (disconnected) to avoid optimistically assuming a connection. */
    TEST_ASSERT_FALSE(bt_manager_is_a2dp_connected());
}

int main(void)
{
    UNITY_BEGIN();

    /* audio_bytes_per_sample */
    RUN_TEST(test_bytes_per_sample_16bit_returns_2);
    RUN_TEST(test_bytes_per_sample_24bit_returns_4);
    RUN_TEST(test_bytes_per_sample_32bit_returns_4);
    RUN_TEST(test_bytes_per_sample_unknown_returns_2);

    /* audio_get_runtime_work_bytes */
    RUN_TEST(test_runtime_work_bytes_uninitialized_returns_default);
    RUN_TEST(test_runtime_work_bytes_set_returns_set_value);
    RUN_TEST(test_runtime_work_bytes_large_value_returned_unchanged);
    /* TEST-2: work-buffer floor constant sanity */
    RUN_TEST(test_work_buffer_dram_min_is_at_least_half_compile_time_default);

    /* weak stub */
    RUN_TEST(test_a2dp_connected_weak_stub_returns_false);

    return UNITY_END();
}
