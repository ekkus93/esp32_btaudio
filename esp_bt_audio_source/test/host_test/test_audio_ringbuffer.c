#include "test_audio_ringbuffer_shared.h"

/* Test fixture (shared with the cases TU via the header). */
audio_rb_t *rb = NULL;

void setUp(void)
{
    /* Each test starts fresh */
    rb = NULL;
}

void tearDown(void)
{
    /* Clean up after each test */
    if (rb != NULL) {
        audio_rb_deinit(rb);
        rb = NULL;
    }
}

//-----------------------------------------------------------------------------
// Unity test runner
//-----------------------------------------------------------------------------

void run_all_tests(void)
{
    /* Basic operations */
    RUN_TEST(test_rb_init_and_capacity);
    RUN_TEST(test_rb_write_and_read_simple);
    RUN_TEST(test_rb_wrap_around);
    RUN_TEST(test_rb_available_counts_correct);
    RUN_TEST(test_rb_peak_tracking);
    
    /* Edge cases */
    RUN_TEST(test_rb_write_when_full_returns_zero);
    RUN_TEST(test_rb_read_when_empty_returns_zero);
    RUN_TEST(test_rb_partial_write_when_insufficient_space);
    RUN_TEST(test_rb_partial_read_when_insufficient_data);
    
    /* Stress */
    RUN_TEST(test_rb_alternating_write_read_many_times);
    RUN_TEST(test_rb_split_writes_across_wrap);
    RUN_TEST(test_rb_wrap_around_read_write_integrity_under_boundary_crossing);
    RUN_TEST(test_rb_watermark_exact_threshold_occupancy_edges);
    RUN_TEST(test_rb_concurrent_producer_consumer_stress);
    
    /* Aggressive stress (Phase 5) */
    RUN_TEST(test_rb_stress_random_size_operations);
    RUN_TEST(test_rb_stress_fill_drain_cycles);
    RUN_TEST(test_rb_stress_alternating_small_large);
    
    /* NULL/invalid parameters */
    RUN_TEST(test_rb_init_rejects_null_pointer);
    RUN_TEST(test_rb_init_rejects_zero_capacity);
    RUN_TEST(test_rb_write_handles_null_rb);
    RUN_TEST(test_rb_read_handles_null_rb);
    RUN_TEST(test_rb_queries_handle_null_rb);
    RUN_TEST(test_rb_deinit_handles_null_safely);
}

int main(void)
{
    UNITY_BEGIN();
    run_all_tests();
    return UNITY_END();
}
