#include "unity.h"

#include <limits.h>
#include <string.h>

#include "hfp_voice_convert.h"

void setUp(void) {}
void tearDown(void) {}

void test_cvsd_duplication_is_exact_and_all_or_nothing(void)
{
    const int16_t input[] = {INT16_MIN, -1, 0, 1, INT16_MAX};
    int16_t output[10] = {0};
    const int16_t expected[] = {
        INT16_MIN, INT16_MIN, -1, -1, 0, 0, 1, 1, INT16_MAX, INT16_MAX};
    TEST_ASSERT_EQUAL(10, hfp_cvsd_8k_to_16k(input, 5, output, 10));
    TEST_ASSERT_EQUAL_INT16_ARRAY(expected, output, 10);
    memset(output, 0x55, sizeof(output));
    TEST_ASSERT_EQUAL(0, hfp_cvsd_8k_to_16k(input, 5, output, 9));
    for (size_t i = 0; i < 10; ++i) TEST_ASSERT_EQUAL_HEX16(0x5555, output[i]);
}

void test_stereo_downmix_handles_extremes_and_cancellation(void)
{
    const int16_t input[] = {
        INT16_MAX, INT16_MAX,
        INT16_MIN, INT16_MIN,
        INT16_MAX, INT16_MIN,
        20000, -10000,
    };
    int16_t output[4] = {0};
    const int16_t expected[] = {INT16_MAX, INT16_MIN, 0, 5000};
    TEST_ASSERT_EQUAL(4, hfp_stereo16_to_mono(input, 4, output, 4));
    TEST_ASSERT_EQUAL_INT16_ARRAY(expected, output, 4);
    TEST_ASSERT_EQUAL(0, hfp_stereo16_to_mono(input, 4, output, 3));
}

void test_converter_rejects_invalid_rates_and_state(void)
{
    hfp_voice_rate_converter_t state;
    size_t consumed = 99;
    size_t produced = 99;
    int16_t sample = 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      hfp_voice_rate_converter_init(NULL, 48000, 8000));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      hfp_voice_rate_converter_init(&state, 0, 8000));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      hfp_voice_rate_converter_init(&state, 8000, 16000));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      hfp_voice_rate_converter_init(&state, 48000, 12000));
    memset(&state, 0, sizeof(state));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      hfp_voice_rate_converter_process(&state, &sample, 1,
                                                       &sample, 1, &consumed,
                                                       &produced));
    TEST_ASSERT_EQUAL(0, consumed);
    TEST_ASSERT_EQUAL(0, produced);
}

void test_48k_to_8k_boxcar_output_is_exact(void)
{
    hfp_voice_rate_converter_t state;
    int16_t input[12];
    int16_t output[2] = {0};
    for (int i = 0; i < 12; ++i) input[i] = (int16_t)i;
    size_t consumed = 0, produced = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_init(&state, 48000, 8000));
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_process(&state, input, 12,
                                                       output, 2, &consumed,
                                                       &produced));
    TEST_ASSERT_EQUAL(12, consumed);
    TEST_ASSERT_EQUAL(2, produced);
    TEST_ASSERT_EQUAL_INT16(2, output[0]);
    TEST_ASSERT_EQUAL_INT16(8, output[1]);
}

void test_48k_to_16k_boxcar_output_is_exact(void)
{
    hfp_voice_rate_converter_t state;
    const int16_t input[] = {3, 6, 9, 12, 15, 18};
    int16_t output[2] = {0};
    size_t consumed = 0, produced = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_init(&state, 48000, 16000));
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_process(&state, input, 6,
                                                       output, 2, &consumed,
                                                       &produced));
    TEST_ASSERT_EQUAL(6, consumed);
    TEST_ASSERT_EQUAL(2, produced);
    TEST_ASSERT_EQUAL_INT16(6, output[0]);
    TEST_ASSERT_EQUAL_INT16(15, output[1]);
}

void test_chunk_boundaries_match_one_shot_conversion(void)
{
    int16_t input[1000];
    for (size_t i = 0; i < 1000; ++i) {
        input[i] = (int16_t)((int)(i % 301U) - 150);
    }
    int16_t one_shot[400] = {0};
    int16_t chunked[400] = {0};
    hfp_voice_rate_converter_t a, b;
    size_t consumed = 0, one_count = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_init(&a, 44100, 16000));
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_init(&b, 44100, 16000));
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_process(&a, input, 1000,
                                                       one_shot, 400,
                                                       &consumed, &one_count));
    TEST_ASSERT_EQUAL(1000, consumed);

    const size_t chunks[] = {1, 7, 13, 2, 61, 5, 109, 3, 211};
    size_t in_pos = 0, out_pos = 0, chunk_index = 0;
    while (in_pos < 1000) {
        size_t count = chunks[chunk_index++ %
                              (sizeof(chunks) / sizeof(chunks[0]))];
        if (count > 1000 - in_pos) count = 1000 - in_pos;
        size_t part_consumed = 0, part_produced = 0;
        TEST_ASSERT_EQUAL(
            ESP_OK,
            hfp_voice_rate_converter_process(&b, input + in_pos, count,
                                             chunked + out_pos,
                                             400 - out_pos,
                                             &part_consumed, &part_produced));
        TEST_ASSERT_EQUAL(count, part_consumed);
        in_pos += part_consumed;
        out_pos += part_produced;
    }
    TEST_ASSERT_EQUAL(one_count, out_pos);
    TEST_ASSERT_EQUAL_INT16_ARRAY(one_shot, chunked, one_count);
}

void test_output_capacity_stops_before_losing_input(void)
{
    hfp_voice_rate_converter_t state;
    int16_t input[12];
    int16_t output[2] = {0};
    for (int i = 0; i < 12; ++i) input[i] = (int16_t)i;
    size_t consumed = 0, produced = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_init(&state, 48000, 8000));
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_process(&state, input, 12,
                                                       output, 1, &consumed,
                                                       &produced));
    TEST_ASSERT_EQUAL(11, consumed);
    TEST_ASSERT_EQUAL(1, produced);
    size_t first_consumed = consumed;
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_process(&state, input + first_consumed,
                                                       12 - first_consumed,
                                                       output + 1, 1,
                                                       &consumed, &produced));
    TEST_ASSERT_EQUAL(1, consumed);
    TEST_ASSERT_EQUAL(1, produced);
    TEST_ASSERT_EQUAL_INT16(2, output[0]);
    TEST_ASSERT_EQUAL_INT16(8, output[1]);
}

void test_reset_removes_partial_previous_window(void)
{
    hfp_voice_rate_converter_t state;
    const int16_t old_input[] = {1000, 1000, 1000, 1000, 1000};
    const int16_t new_input[] = {6, 6, 6, 6, 6, 6};
    int16_t output = 0;
    size_t consumed = 0, produced = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_init(&state, 48000, 8000));
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_process(&state, old_input, 5,
                                                       &output, 1, &consumed,
                                                       &produced));
    TEST_ASSERT_EQUAL(5, consumed);
    TEST_ASSERT_EQUAL(0, produced);
    hfp_voice_rate_converter_reset(&state);
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_process(&state, new_input, 6,
                                                       &output, 1, &consumed,
                                                       &produced));
    TEST_ASSERT_EQUAL(6, consumed);
    TEST_ASSERT_EQUAL(1, produced);
    TEST_ASSERT_EQUAL_INT16(6, output);
}

void test_insufficient_input_produces_no_fabricated_output(void)
{
    hfp_voice_rate_converter_t state;
    const int16_t input[] = {1, 2, 3, 4, 5};
    int16_t output = (int16_t)0x5555;
    size_t consumed = 0, produced = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_init(&state, 48000, 8000));
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_process(&state, input, 5,
                                                       &output, 1, &consumed,
                                                       &produced));
    TEST_ASSERT_EQUAL(5, consumed);
    TEST_ASSERT_EQUAL(0, produced);
    TEST_ASSERT_EQUAL_HEX16(0x5555, output);
}

void test_equal_rate_is_exact_passthrough(void)
{
    hfp_voice_rate_converter_t state;
    const int16_t input[] = {INT16_MIN, -10, 0, 10, INT16_MAX};
    int16_t output[5] = {0};
    size_t consumed = 0, produced = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_init(&state, 16000, 16000));
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_process(&state, input, 5,
                                                       output, 5, &consumed,
                                                       &produced));
    TEST_ASSERT_EQUAL(5, consumed);
    TEST_ASSERT_EQUAL(5, produced);
    TEST_ASSERT_EQUAL_INT16_ARRAY(input, output, 5);
}

void test_zero_output_capacity_consumes_nothing(void)
{
    hfp_voice_rate_converter_t state;
    const int16_t input[] = {1, 2, 3, 4, 5, 6};
    size_t consumed = 99, produced = 99;
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_init(&state, 48000, 8000));
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_voice_rate_converter_process(&state, input, 6,
                                                       NULL, 0, &consumed,
                                                       &produced));
    TEST_ASSERT_EQUAL(0, consumed);
    TEST_ASSERT_EQUAL(0, produced);
    TEST_ASSERT_EQUAL_UINT32(0, state.phase);
    TEST_ASSERT_EQUAL_UINT32(0, state.window_samples);
}
