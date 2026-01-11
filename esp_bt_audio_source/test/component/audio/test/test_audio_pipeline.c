#include "unity.h"
#include "audio_pipeline.h"
#include "esp_err.h"
#include <limits.h>
#include <string.h>

static void test_buffer_pool_lifecycle(void)
{
    audio_buffer_cfg_t cfg = {.buffer_count = 2, .buffer_size = 8};
    audio_buffer_pool_t *pool = audio_buffer_pool_init(&cfg);
    TEST_ASSERT_NOT_NULL(pool);
    TEST_ASSERT_TRUE(audio_buffer_pool_is_initialized());

    audio_buffer_t *b1 = audio_buffer_alloc(pool);
    audio_buffer_t *b2 = audio_buffer_alloc(pool);
    TEST_ASSERT_NOT_NULL(b1);
    TEST_ASSERT_NOT_NULL(b2);
    /* Pool exhausted */
    TEST_ASSERT_NULL(audio_buffer_alloc(pool));

    /* Release one and ensure it can be reallocated */
    TEST_ASSERT_EQUAL(ESP_OK, audio_buffer_release(pool, b1));
    audio_buffer_t *b3 = audio_buffer_alloc(pool);
    TEST_ASSERT_NOT_NULL(b3);

    /* Double release should flag invalid state */
    TEST_ASSERT_EQUAL(ESP_OK, audio_buffer_release(pool, b3));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, audio_buffer_release(pool, b3));

    audio_buffer_pool_deinit(pool);
    TEST_ASSERT_FALSE(audio_buffer_pool_is_initialized());
}

static void test_buffer_pool_invalid_inputs(void)
{
    audio_buffer_cfg_t bad_cfg = {.buffer_count = 0, .buffer_size = 16};
    TEST_ASSERT_NULL(audio_buffer_pool_init(&bad_cfg));
    audio_buffer_cfg_t bad_size = {.buffer_count = 2, .buffer_size = 0};
    TEST_ASSERT_NULL(audio_buffer_pool_init(&bad_size));

    audio_buffer_pool_t dummy_pool = {0};
    audio_buffer_t foreign = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_buffer_release(&dummy_pool, &foreign));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_buffer_release(NULL, &foreign));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_buffer_release(&dummy_pool, NULL));
}

static void test_buffer_read_write(void)
{
    audio_buffer_t buf = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_buffer_init(NULL, 8));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_buffer_init(&buf, 0));
    TEST_ASSERT_EQUAL(ESP_OK, audio_buffer_init(&buf, 8));

    uint8_t data1[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL(ESP_OK, audio_buffer_write(&buf, data1, sizeof(data1)));
    /* Overflow attempt */
    uint8_t big[5] = {5, 6, 7, 8, 9};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, audio_buffer_write(&buf, big, sizeof(big)));

    uint8_t out[4] = {0};
    TEST_ASSERT_EQUAL(ESP_OK, audio_buffer_read(&buf, out, 2));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data1, out, 2);

    /* Reading more than available should clamp */
    memset(out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(ESP_OK, audio_buffer_read(&buf, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(3, out[0]);
    TEST_ASSERT_EQUAL_UINT8(4, out[1]);

    TEST_ASSERT_EQUAL(ESP_OK, audio_buffer_deinit(&buf));
    /* Double deinit is allowed and returns OK */
    TEST_ASSERT_EQUAL(ESP_OK, audio_buffer_deinit(&buf));
}

static void test_audio_pipeline_process_volume_and_eq(void)
{
    audio_pipeline_t *pipeline = audio_pipeline_init();
    TEST_ASSERT_NOT_NULL(pipeline);
    TEST_ASSERT_EQUAL(ESP_OK, audio_pipeline_add_volume_stage(pipeline, 0.5f));
    TEST_ASSERT_EQUAL(ESP_OK, audio_pipeline_add_eq_stage(pipeline, 4.0f, 1000.0f));

    int16_t input_samples[4] = {1000, -1000, INT16_MAX, INT16_MIN};
    audio_buffer_t in = {.data = input_samples, .size = sizeof(input_samples), .length = sizeof(input_samples)};
    int16_t output_samples[4] = {0};
    audio_buffer_t out = {.data = output_samples, .size = sizeof(output_samples), .length = 0};

    TEST_ASSERT_EQUAL(ESP_OK, audio_pipeline_process(pipeline, &in, &out));
    TEST_ASSERT_EQUAL(sizeof(output_samples), out.length);

    /* Volume 0.5 then EQ gain 4 => net gain 2; expect clipping */
    TEST_ASSERT_INT16_WITHIN(1, 2000, output_samples[0]);
    TEST_ASSERT_INT16_WITHIN(1, -2000, output_samples[1]);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, output_samples[2]);
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, output_samples[3]);

    audio_pipeline_deinit(pipeline);
}

static void test_audio_pipeline_invalid_args(void)
{
    audio_pipeline_t *pipeline = audio_pipeline_init();
    audio_buffer_t buf = {0};
    int16_t data[2] = {1, 2};
    buf.data = data;
    buf.size = sizeof(data);
    buf.length = sizeof(data);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_pipeline_process(NULL, &buf, &buf));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_pipeline_process(pipeline, NULL, &buf));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_pipeline_process(pipeline, &buf, NULL));

    audio_pipeline_deinit(pipeline);
}

static void test_audio_volume_controls(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, audio_volume_set(1.5f));
    float vol = -1.0f;
    TEST_ASSERT_EQUAL(ESP_OK, audio_volume_get(&vol));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, vol);

    TEST_ASSERT_EQUAL(ESP_OK, audio_volume_set(-0.5f));
    TEST_ASSERT_EQUAL(ESP_OK, audio_volume_get(&vol));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, vol);

    int16_t input[3] = {1000, -2000, 5000};
    int16_t output[3] = {0};
    TEST_ASSERT_EQUAL(ESP_OK, audio_volume_apply(input, output, 3));
    TEST_ASSERT_EQUAL_INT16(0, output[0]);

    TEST_ASSERT_EQUAL(ESP_OK, audio_volume_set(0.5f));
    TEST_ASSERT_EQUAL(ESP_OK, audio_volume_mute());
    TEST_ASSERT_EQUAL(ESP_OK, audio_volume_apply(input, output, 3));
    TEST_ASSERT_EACH_EQUAL_INT16(0, output, 3);
    TEST_ASSERT_EQUAL(ESP_OK, audio_volume_unmute());
    TEST_ASSERT_EQUAL(ESP_OK, audio_volume_apply(input, output, 3));
    TEST_ASSERT_INT16_WITHIN(1, 500, output[0]);
}

static void test_audio_sample_rate_and_size_helpers(void)
{
    int rate = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_configure_sample_rate(12345));
    TEST_ASSERT_EQUAL(ESP_OK, audio_configure_sample_rate(44100));
    TEST_ASSERT_EQUAL(ESP_OK, audio_get_sample_rate(&rate));
    TEST_ASSERT_EQUAL(44100, rate);

    size_t size = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_calculate_buffer_size(0, 44100, &size));
    TEST_ASSERT_EQUAL(ESP_OK, audio_calculate_buffer_size(100, 44100, &size));
    TEST_ASSERT_EQUAL((size_t)17640, size); /* 100 ms, stereo 16-bit */

    int16_t src[6] = {1, 2, 3, 4, 5, 6};
    int16_t dst[3] = {0};
    /* Downsample 48k -> 24k using simple picker */
    TEST_ASSERT_EQUAL(ESP_OK, audio_convert_sample_rate(src, sizeof(src), 48000, dst, sizeof(dst), 24000));
    TEST_ASSERT_EQUAL_INT16(1, dst[0]);
    TEST_ASSERT_EQUAL_INT16(3, dst[1]);
    TEST_ASSERT_EQUAL_INT16(5, dst[2]);
}

void audio_pipeline_tests_register(void)
{
    RUN_TEST(test_buffer_pool_lifecycle);
    RUN_TEST(test_buffer_pool_invalid_inputs);
    RUN_TEST(test_buffer_read_write);
    RUN_TEST(test_audio_pipeline_process_volume_and_eq);
    RUN_TEST(test_audio_pipeline_invalid_args);
    RUN_TEST(test_audio_volume_controls);
    RUN_TEST(test_audio_sample_rate_and_size_helpers);
}
