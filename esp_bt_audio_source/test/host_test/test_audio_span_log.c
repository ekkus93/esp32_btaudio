/**
 * @file test_audio_span_log.c
 * @brief Unit tests for audio_span_log.c (circular buffer diagnostic logging)
 *
 * Tests cover:
 * - Initialization (success, failure, double init)
 * - Push operations (wraparound, overflow, not initialized)
 * - Get last N (partial/full buffer, wraparound, edge cases)
 * - Reset, capacity, count operations
 *
 * TDD Phase: Section 8.3 - Untested Supporting Code
 */

#include "unity.h"
#include "audio_span_log.h"
#include <string.h>
#include <stdlib.h>

/* Test fixtures */
void setUp(void) {
    /* Ensure clean state before each test */
    span_log_deinit();
}

void tearDown(void) {
    /* Clean up after each test */
    span_log_deinit();
}

/* ============================================================================
 * 1. Initialization Tests
 * ========================================================================= */

void test_span_log_init_zero_capacity_should_fail(void) {
    /* Arrange & Act */
    bool result = span_log_init(0);
    
    /* Assert */
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL(0, span_log_capacity());
    TEST_ASSERT_EQUAL(0, span_log_count());
}

void test_span_log_init_valid_capacity_should_succeed(void) {
    /* Arrange & Act */
    bool result = span_log_init(10);
    
    /* Assert */
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(10, span_log_capacity());
    TEST_ASSERT_EQUAL(0, span_log_count());
}

void test_span_log_init_double_init_should_fail(void) {
    /* Arrange */
    TEST_ASSERT_TRUE(span_log_init(5));
    
    /* Act - second init should fail */
    bool result = span_log_init(10);
    
    /* Assert */
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL(5, span_log_capacity());  /* Original capacity preserved */
}

/* ============================================================================
 * 2. Deinitialization Tests
 * ========================================================================= */

void test_span_log_deinit_when_initialized_should_cleanup(void) {
    /* Arrange */
    TEST_ASSERT_TRUE(span_log_init(10));
    span_log_push(1, 100, 512, 1024, 1, 0);
    TEST_ASSERT_EQUAL(1, span_log_count());
    
    /* Act */
    span_log_deinit();
    
    /* Assert */
    TEST_ASSERT_EQUAL(0, span_log_capacity());
    TEST_ASSERT_EQUAL(0, span_log_count());
}

void test_span_log_deinit_when_not_initialized_should_be_safe(void) {
    /* Act - should not crash */
    span_log_deinit();
    
    /* Assert */
    TEST_ASSERT_EQUAL(0, span_log_capacity());
    TEST_ASSERT_EQUAL(0, span_log_count());
}

void test_span_log_deinit_multiple_calls_should_be_safe(void) {
    /* Arrange */
    TEST_ASSERT_TRUE(span_log_init(5));
    
    /* Act - multiple deinits should be safe */
    span_log_deinit();
    span_log_deinit();
    span_log_deinit();
    
    /* Assert */
    TEST_ASSERT_EQUAL(0, span_log_capacity());
}

/* ============================================================================
 * 3. Push Operations Tests
 * ========================================================================= */

void test_span_log_push_not_initialized_should_be_silent(void) {
    /* Act - should not crash when not initialized */
    span_log_push(1, 100, 512, 1024, 1, 0);
    
    /* Assert */
    TEST_ASSERT_EQUAL(0, span_log_count());
}

void test_span_log_push_single_entry_should_increment_count(void) {
    /* Arrange */
    TEST_ASSERT_TRUE(span_log_init(10));
    
    /* Act */
    span_log_push(42, 100, 512, 1024, 1, 0xFF);
    
    /* Assert */
    TEST_ASSERT_EQUAL(1, span_log_count());
    
    audio_rb_span_t entry;
    size_t actual;
    TEST_ASSERT_TRUE(span_log_get_last_n(&entry, 1, &actual));
    TEST_ASSERT_EQUAL(1, actual);
    TEST_ASSERT_EQUAL(42, entry.seq);
    TEST_ASSERT_EQUAL(100, entry.timestamp_ms);
    TEST_ASSERT_EQUAL(512, entry.bytes);
    TEST_ASSERT_EQUAL(1, entry.ring_used_kb);  /* (1024 + 512) / 1024 = 1 */
    TEST_ASSERT_EQUAL(1, entry.source);
    TEST_ASSERT_EQUAL(0xFF, entry.flags);
}

void test_span_log_push_fill_to_capacity_should_saturate_count(void) {
    /* Arrange */
    TEST_ASSERT_TRUE(span_log_init(3));
    
    /* Act - fill to capacity */
    span_log_push(1, 100, 512, 1024, 1, 0);
    span_log_push(2, 200, 512, 2048, 1, 0);
    span_log_push(3, 300, 512, 3072, 1, 0);
    
    /* Assert */
    TEST_ASSERT_EQUAL(3, span_log_count());
    TEST_ASSERT_EQUAL(3, span_log_capacity());
}

void test_span_log_push_overflow_should_overwrite_oldest(void) {
    /* Arrange */
    TEST_ASSERT_TRUE(span_log_init(3));
    span_log_push(1, 100, 512, 1024, 1, 0);
    span_log_push(2, 200, 512, 2048, 1, 0);
    span_log_push(3, 300, 512, 3072, 1, 0);
    
    /* Act - overflow (should overwrite entry 1) */
    span_log_push(4, 400, 512, 4096, 1, 0);
    
    /* Assert */
    TEST_ASSERT_EQUAL(3, span_log_count());  /* Count still saturated */
    
    /* Retrieve all 3 entries - should get 2, 3, 4 (oldest=1 was overwritten) */
    audio_rb_span_t entries[3];
    size_t actual;
    TEST_ASSERT_TRUE(span_log_get_last_n(entries, 3, &actual));
    TEST_ASSERT_EQUAL(3, actual);
    TEST_ASSERT_EQUAL(2, entries[0].seq);
    TEST_ASSERT_EQUAL(3, entries[1].seq);
    TEST_ASSERT_EQUAL(4, entries[2].seq);
}

void test_span_log_push_ring_used_kb_rounding(void) {
    /* Arrange */
    TEST_ASSERT_TRUE(span_log_init(5));
    
    /* Act - test rounding: (bytes + 512) / 1024 */
    span_log_push(1, 100, 512, 0,    1, 0);  /* (0 + 512) / 1024 = 0 */
    span_log_push(2, 200, 512, 511,  1, 0);  /* (511 + 512) / 1024 = 0 */
    span_log_push(3, 300, 512, 512,  1, 0);  /* (512 + 512) / 1024 = 1 */
    span_log_push(4, 400, 512, 1023, 1, 0);  /* (1023 + 512) / 1024 = 1 */
    span_log_push(5, 500, 512, 1024, 1, 0);  /* (1024 + 512) / 1024 = 1 */
    
    /* Assert */
    audio_rb_span_t entries[5];
    size_t actual;
    TEST_ASSERT_TRUE(span_log_get_last_n(entries, 5, &actual));
    TEST_ASSERT_EQUAL(5, actual);
    TEST_ASSERT_EQUAL(0, entries[0].ring_used_kb);
    TEST_ASSERT_EQUAL(0, entries[1].ring_used_kb);
    TEST_ASSERT_EQUAL(1, entries[2].ring_used_kb);
    TEST_ASSERT_EQUAL(1, entries[3].ring_used_kb);
    TEST_ASSERT_EQUAL(1, entries[4].ring_used_kb);
}

/* ============================================================================
 * 4. Get Last N Tests
 * ========================================================================= */

void test_span_log_get_last_n_not_initialized_should_fail(void) {
    /* Arrange */
    audio_rb_span_t entry;
    size_t actual;
    
    /* Act */
    bool result = span_log_get_last_n(&entry, 1, &actual);
    
    /* Assert */
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL(0, actual);
}

void test_span_log_get_last_n_null_out_pointer_should_fail(void) {
    /* Arrange */
    TEST_ASSERT_TRUE(span_log_init(5));
    span_log_push(1, 100, 512, 1024, 1, 0);
    size_t actual;
    
    /* Act */
    bool result = span_log_get_last_n(NULL, 1, &actual);
    
    /* Assert */
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL(0, actual);
}

void test_span_log_get_last_n_empty_buffer_should_return_zero(void) {
    /* Arrange */
    TEST_ASSERT_TRUE(span_log_init(5));
    audio_rb_span_t entry;
    size_t actual;
    
    /* Act */
    bool result = span_log_get_last_n(&entry, 1, &actual);
    
    /* Assert */
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(0, actual);
}

void test_span_log_get_last_n_partial_buffer_should_return_all(void) {
    /* Arrange */
    TEST_ASSERT_TRUE(span_log_init(10));
    span_log_push(1, 100, 512, 1024, 1, 0);
    span_log_push(2, 200, 512, 2048, 1, 0);
    span_log_push(3, 300, 512, 3072, 1, 0);
    
    /* Act - request 5, but only 3 available */
    audio_rb_span_t entries[5];
    size_t actual;
    bool result = span_log_get_last_n(entries, 5, &actual);
    
    /* Assert */
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(3, actual);
    TEST_ASSERT_EQUAL(1, entries[0].seq);
    TEST_ASSERT_EQUAL(2, entries[1].seq);
    TEST_ASSERT_EQUAL(3, entries[2].seq);
}

void test_span_log_get_last_n_full_buffer_wraparound(void) {
    /* Arrange - fill buffer, then overflow to test wraparound */
    TEST_ASSERT_TRUE(span_log_init(4));
    span_log_push(1, 100, 512, 1024, 1, 0);
    span_log_push(2, 200, 512, 2048, 1, 0);
    span_log_push(3, 300, 512, 3072, 1, 0);
    span_log_push(4, 400, 512, 4096, 1, 0);
    span_log_push(5, 500, 512, 5120, 1, 0);  /* Overwrites entry 1 */
    span_log_push(6, 600, 512, 6144, 1, 0);  /* Overwrites entry 2 */
    
    /* Act - get last 4 (should be 3, 4, 5, 6) */
    audio_rb_span_t entries[4];
    size_t actual;
    TEST_ASSERT_TRUE(span_log_get_last_n(entries, 4, &actual));
    
    /* Assert */
    TEST_ASSERT_EQUAL(4, actual);
    TEST_ASSERT_EQUAL(3, entries[0].seq);
    TEST_ASSERT_EQUAL(4, entries[1].seq);
    TEST_ASSERT_EQUAL(5, entries[2].seq);
    TEST_ASSERT_EQUAL(6, entries[3].seq);
}

void test_span_log_get_last_n_request_subset_from_full_buffer(void) {
    /* Arrange */
    TEST_ASSERT_TRUE(span_log_init(5));
    span_log_push(1, 100, 512, 1024, 1, 0);
    span_log_push(2, 200, 512, 2048, 1, 0);
    span_log_push(3, 300, 512, 3072, 1, 0);
    span_log_push(4, 400, 512, 4096, 1, 0);
    span_log_push(5, 500, 512, 5120, 1, 0);
    
    /* Act - request last 2 entries (should be 4, 5) */
    audio_rb_span_t entries[2];
    size_t actual;
    TEST_ASSERT_TRUE(span_log_get_last_n(entries, 2, &actual));
    
    /* Assert */
    TEST_ASSERT_EQUAL(2, actual);
    TEST_ASSERT_EQUAL(4, entries[0].seq);
    TEST_ASSERT_EQUAL(5, entries[1].seq);
}

void test_span_log_get_last_n_null_actual_pointer_should_succeed(void) {
    /* Arrange */
    TEST_ASSERT_TRUE(span_log_init(5));
    span_log_push(1, 100, 512, 1024, 1, 0);
    audio_rb_span_t entry;
    
    /* Act - NULL actual pointer should be allowed */
    bool result = span_log_get_last_n(&entry, 1, NULL);
    
    /* Assert */
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1, entry.seq);
}

/* ============================================================================
 * 5. Reset, Capacity, Count Tests
 * ========================================================================= */

void test_span_log_reset_should_clear_count_preserve_capacity(void) {
    /* Arrange */
    TEST_ASSERT_TRUE(span_log_init(5));
    span_log_push(1, 100, 512, 1024, 1, 0);
    span_log_push(2, 200, 512, 2048, 1, 0);
    TEST_ASSERT_EQUAL(2, span_log_count());
    
    /* Act */
    span_log_reset();
    
    /* Assert */
    TEST_ASSERT_EQUAL(5, span_log_capacity());  /* Capacity unchanged */
    TEST_ASSERT_EQUAL(0, span_log_count());     /* Count reset */
}

void test_span_log_reset_not_initialized_should_be_safe(void) {
    /* Act - should not crash */
    span_log_reset();
    
    /* Assert */
    TEST_ASSERT_EQUAL(0, span_log_count());
}

void test_span_log_capacity_not_initialized_should_return_zero(void) {
    /* Act & Assert */
    TEST_ASSERT_EQUAL(0, span_log_capacity());
}

void test_span_log_count_not_initialized_should_return_zero(void) {
    /* Act & Assert */
    TEST_ASSERT_EQUAL(0, span_log_count());
}

/* ============================================================================
 * Test Runner
 * ========================================================================= */

int main(void) {
    UNITY_BEGIN();
    
    /* Initialization tests */
    RUN_TEST(test_span_log_init_zero_capacity_should_fail);
    RUN_TEST(test_span_log_init_valid_capacity_should_succeed);
    RUN_TEST(test_span_log_init_double_init_should_fail);
    
    /* Deinitialization tests */
    RUN_TEST(test_span_log_deinit_when_initialized_should_cleanup);
    RUN_TEST(test_span_log_deinit_when_not_initialized_should_be_safe);
    RUN_TEST(test_span_log_deinit_multiple_calls_should_be_safe);
    
    /* Push tests */
    RUN_TEST(test_span_log_push_not_initialized_should_be_silent);
    RUN_TEST(test_span_log_push_single_entry_should_increment_count);
    RUN_TEST(test_span_log_push_fill_to_capacity_should_saturate_count);
    RUN_TEST(test_span_log_push_overflow_should_overwrite_oldest);
    RUN_TEST(test_span_log_push_ring_used_kb_rounding);
    
    /* Get last N tests */
    RUN_TEST(test_span_log_get_last_n_not_initialized_should_fail);
    RUN_TEST(test_span_log_get_last_n_null_out_pointer_should_fail);
    RUN_TEST(test_span_log_get_last_n_empty_buffer_should_return_zero);
    RUN_TEST(test_span_log_get_last_n_partial_buffer_should_return_all);
    RUN_TEST(test_span_log_get_last_n_full_buffer_wraparound);
    RUN_TEST(test_span_log_get_last_n_request_subset_from_full_buffer);
    RUN_TEST(test_span_log_get_last_n_null_actual_pointer_should_succeed);
    
    /* Reset/capacity/count tests */
    RUN_TEST(test_span_log_reset_should_clear_count_preserve_capacity);
    RUN_TEST(test_span_log_reset_not_initialized_should_be_safe);
    RUN_TEST(test_span_log_capacity_not_initialized_should_return_zero);
    RUN_TEST(test_span_log_count_not_initialized_should_return_zero);
    
    return UNITY_END();
}
