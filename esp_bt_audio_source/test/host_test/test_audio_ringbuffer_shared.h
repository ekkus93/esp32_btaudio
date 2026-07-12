/* test_audio_ringbuffer_shared.h — shared decls for the split
 * test_audio_ringbuffer executable (runner + cases). Not a public header. */
#ifndef TEST_AUDIO_RINGBUFFER_SHARED_H
#define TEST_AUDIO_RINGBUFFER_SHARED_H

/**
 * @file test_audio_ringbuffer.c
 * @brief Unit tests for audio ring buffer (SPSC)
 *
 * Tests cover:
 * - Basic operations (init, write, read, capacity queries)
 * - Edge cases (empty, full, partial operations)
 * - Wrap-around behavior
 * - Peak tracking
 * - Stress scenarios
 *
 * CODE_REVIEW6 Phase 1, Task 1.2
 */

#include "unity.h"
#include "audio_ringbuffer.h"
#include <string.h>
#include <pthread.h>
#include <sched.h>


/* Fixture — DEFINED in test_audio_ringbuffer.c (runner). */
extern audio_rb_t *rb;

/* Test bodies live in test_audio_ringbuffer_cases.c */
void test_rb_init_and_capacity(void);
void test_rb_write_and_read_simple(void);
void test_rb_wrap_around(void);
void test_rb_available_counts_correct(void);
void test_rb_peak_tracking(void);
void test_rb_write_when_full_returns_zero(void);
void test_rb_read_when_empty_returns_zero(void);
void test_rb_partial_write_when_insufficient_space(void);
void test_rb_partial_read_when_insufficient_data(void);
void test_rb_alternating_write_read_many_times(void);
void test_rb_split_writes_across_wrap(void);
void test_rb_wrap_around_read_write_integrity_under_boundary_crossing(void);
void test_rb_watermark_exact_threshold_occupancy_edges(void);
void test_rb_concurrent_producer_consumer_stress(void);
void test_rb_stress_random_size_operations(void);
void test_rb_stress_fill_drain_cycles(void);
void test_rb_stress_alternating_small_large(void);
void test_rb_init_rejects_null_pointer(void);
void test_rb_init_rejects_zero_capacity(void);
void test_rb_write_handles_null_rb(void);
void test_rb_read_handles_null_rb(void);
void test_rb_queries_handle_null_rb(void);
void test_rb_deinit_handles_null_safely(void);

#endif /* TEST_AUDIO_RINGBUFFER_SHARED_H */
