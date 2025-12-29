#include "unity.h"

#include <string.h>
#include <stdint.h>

#include "beep_manager.h"
#include "audio_queue.h"
#include "freertos/task.h"

#include <pthread.h>

/* Shim helpers exposed by shim_audio_queue.c */
size_t audio_queue_last_len(void);
audio_source_tag_t audio_queue_last_tag(void);
uint16_t audio_queue_last_tag_id(void);
const uint8_t *audio_queue_last_data(void);

static bool s_done_called = false;

static void on_done(void *ctx)
{
    bool *flag = (bool *)ctx;
    if (flag) {
        *flag = true;
    }
}

static audio_config_t make_cfg_16k_mono(void)
{
    audio_config_t cfg = {0};
    cfg.sample_rate = AUDIO_SAMPLE_RATE_16K;
    cfg.bit_depth = AUDIO_BIT_DEPTH_16;
    cfg.channels = AUDIO_CHANNEL_MONO;
    cfg.volume = 80;
    return cfg;
}

static beep_request_t make_req_defaults(void)
{
    beep_request_t req = {0};
    req.duration_ms = 10;
    req.freq_hz = 1000.0;
    req.amplitude = 1000;
    return req;
}

void setUp(void)
{
    s_done_called = false;
    audio_chunk_clear();
    audio_chunk_pool_deinit();
    beep_manager_deinit();
}

void tearDown(void)
{
    audio_chunk_pool_deinit();
    beep_manager_deinit();
}

void test_init_should_be_idempotent(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_init());
    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_init());
}

void test_play_should_enqueue_and_invoke_callback(void)
{
    audio_config_t cfg = make_cfg_16k_mono();
    beep_request_t req = make_req_defaults();

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_init());
    beep_manager_set_done_callback(on_done, &s_done_called);

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &cfg));

    TEST_ASSERT_TRUE(s_done_called);
    TEST_ASSERT_EQUAL(BEEP_STATE_STOPPED, beep_manager_get_state());
    TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_BEEP, audio_queue_last_tag());
    TEST_ASSERT_GREATER_THAN_UINT(0, audio_queue_last_len());
}

void test_play_should_reject_invalid_args(void)
{
    audio_config_t cfg = make_cfg_16k_mono();
    beep_request_t req = make_req_defaults();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, beep_manager_play(NULL, &cfg));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, beep_manager_play(&req, NULL));
}

void test_play_should_reject_unsupported_bit_depth(void)
{
    audio_config_t cfg = make_cfg_16k_mono();
    cfg.bit_depth = AUDIO_BIT_DEPTH_24;
    beep_request_t req = make_req_defaults();

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_init());
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, beep_manager_play(&req, &cfg));
    TEST_ASSERT_EQUAL(BEEP_STATE_STOPPED, beep_manager_get_state());
    TEST_ASSERT_EQUAL(0U, audio_queue_last_len());
}

void test_tag_id_should_increase_across_plays(void)
{
    audio_config_t cfg = make_cfg_16k_mono();
    beep_request_t req = make_req_defaults();

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_init());
    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &cfg));
    uint16_t first_tag = audio_queue_last_tag_id();

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &cfg));
    uint16_t second_tag = audio_queue_last_tag_id();

    TEST_ASSERT_TRUE(second_tag > first_tag);
}

void test_duration_should_clamp_to_max(void)
{
    audio_config_t cfg = make_cfg_16k_mono();
    beep_request_t req = make_req_defaults();
    req.duration_ms = 60000; /* well above max */

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_init());
    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &cfg));

    /* 20s at 16 kHz, 512 frames per chunk -> ~625 chunks => last tag >= 600 */
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(600, audio_queue_last_tag_id());
}

void test_play_should_produce_stereo_32bit_and_keep_channels_equal(void)
{
    audio_config_t cfg = {0};
    cfg.sample_rate = AUDIO_SAMPLE_RATE_16K;
    cfg.bit_depth = AUDIO_BIT_DEPTH_32;
    cfg.channels = AUDIO_CHANNEL_STEREO;
    cfg.volume = 60;

    beep_request_t req = make_req_defaults();
    req.duration_ms = 10; /* small to keep two chunks */

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_init());
    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &cfg));

    /* Expect two chunks: 1280 bytes total -> last chunk 256 bytes */
    TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_BEEP, audio_queue_last_tag());
    TEST_ASSERT_EQUAL_size_t(256, audio_queue_last_len());
    const int32_t *samples = (const int32_t *)audio_queue_last_data();
    size_t sample_pairs = audio_queue_last_len() / sizeof(int32_t) / 2;
    for (size_t i = 0; i < sample_pairs; ++i) {
        int32_t left = samples[i * 2];
        int32_t right = samples[i * 2 + 1];
        TEST_ASSERT_EQUAL_INT32(left, right);
    }
}

static void *beep_thread(void *arg)
{
    (void)arg;
    audio_config_t cfg = make_cfg_16k_mono();
    beep_request_t req = make_req_defaults();
    req.duration_ms = 500; /* long enough to be interrupted */
    beep_manager_play(&req, &cfg);
    return NULL;
}

void test_stop_should_interrupt_play_and_clear_flag_for_next_play(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_init());

    pthread_t tid;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&tid, NULL, beep_thread, NULL));
    /* Give the worker a moment to start generating chunks. */
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 2 * 1000 * 1000};
    nanosleep(&ts, NULL);

    beep_manager_stop();
    TEST_ASSERT_EQUAL_INT(0, pthread_join(tid, NULL));

    /* Should have enqueued at least one chunk but not hundreds. */
    TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_BEEP, audio_queue_last_tag());
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(50, audio_queue_last_tag_id());

    /* stop_requested must not block subsequent plays */
    beep_request_t req = make_req_defaults();
    audio_config_t cfg = make_cfg_16k_mono();
    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &cfg));
    TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_BEEP, audio_queue_last_tag());
}

void test_stop_should_be_noop_when_already_stopped(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_init());
    beep_manager_stop();

    beep_request_t req = make_req_defaults();
    req.duration_ms = 1; /* minimal duration */
    audio_config_t cfg = make_cfg_16k_mono();

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &cfg));
    TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_BEEP, audio_queue_last_tag());
    TEST_ASSERT_GREATER_THAN_UINT(0, audio_queue_last_len());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_should_be_idempotent);
    RUN_TEST(test_play_should_enqueue_and_invoke_callback);
    RUN_TEST(test_play_should_reject_invalid_args);
    RUN_TEST(test_play_should_reject_unsupported_bit_depth);
    RUN_TEST(test_tag_id_should_increase_across_plays);
    RUN_TEST(test_duration_should_clamp_to_max);
    RUN_TEST(test_play_should_produce_stereo_32bit_and_keep_channels_equal);
    RUN_TEST(test_stop_should_interrupt_play_and_clear_flag_for_next_play);
    RUN_TEST(test_stop_should_be_noop_when_already_stopped);
    return UNITY_END();
}
