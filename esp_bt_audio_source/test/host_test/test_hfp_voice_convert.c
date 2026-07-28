#include "unity.h"

void test_cvsd_duplication_is_exact_and_all_or_nothing(void);
void test_stereo_downmix_handles_extremes_and_cancellation(void);
void test_converter_rejects_invalid_rates_and_state(void);
void test_48k_to_8k_boxcar_output_is_exact(void);
void test_48k_to_16k_boxcar_output_is_exact(void);
void test_chunk_boundaries_match_one_shot_conversion(void);
void test_output_capacity_stops_before_losing_input(void);
void test_reset_removes_partial_previous_window(void);
void test_insufficient_input_produces_no_fabricated_output(void);
void test_equal_rate_is_exact_passthrough(void);
void test_zero_output_capacity_consumes_nothing(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_cvsd_duplication_is_exact_and_all_or_nothing);
    RUN_TEST(test_stereo_downmix_handles_extremes_and_cancellation);
    RUN_TEST(test_converter_rejects_invalid_rates_and_state);
    RUN_TEST(test_48k_to_8k_boxcar_output_is_exact);
    RUN_TEST(test_48k_to_16k_boxcar_output_is_exact);
    RUN_TEST(test_chunk_boundaries_match_one_shot_conversion);
    RUN_TEST(test_output_capacity_stops_before_losing_input);
    RUN_TEST(test_reset_removes_partial_previous_window);
    RUN_TEST(test_insufficient_input_produces_no_fabricated_output);
    RUN_TEST(test_equal_rate_is_exact_passthrough);
    RUN_TEST(test_zero_output_capacity_consumes_nothing);
    return UNITY_END();
}
