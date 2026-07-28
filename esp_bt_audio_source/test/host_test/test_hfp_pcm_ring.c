#include "unity.h"

void test_ring_init_rejects_invalid_arguments(void);
void test_ring_simple_write_and_read(void);
void test_ring_exact_full_and_frame_rejection_is_atomic(void);
void test_ring_wraparound_preserves_bytes(void);
void test_ring_underflow_and_overflow_counters_are_exact(void);
void test_ring_generation_and_reset_contract(void);
void test_ring_invalid_operations_are_visible(void);
void test_ring_spsc_stress_preserves_order(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ring_init_rejects_invalid_arguments);
    RUN_TEST(test_ring_simple_write_and_read);
    RUN_TEST(test_ring_exact_full_and_frame_rejection_is_atomic);
    RUN_TEST(test_ring_wraparound_preserves_bytes);
    RUN_TEST(test_ring_underflow_and_overflow_counters_are_exact);
    RUN_TEST(test_ring_generation_and_reset_contract);
    RUN_TEST(test_ring_invalid_operations_are_visible);
    RUN_TEST(test_ring_spsc_stress_preserves_order);
    return UNITY_END();
}
