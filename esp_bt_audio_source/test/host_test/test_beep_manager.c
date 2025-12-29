#include "unity.h"
#include "beep_manager.h"
#include "audio_queue.h"

static bool s_done_called = false;

static void on_done(void *ctx)
{
    bool *flag = (bool *)ctx;
    if (flag) {
        *flag = true;
    }
}

void setUp(void)
{
    s_done_called = false;
    audio_chunk_pool_deinit();
    beep_manager_deinit();
}

void tearDown(void)
{
    audio_chunk_pool_deinit();
    beep_manager_deinit();
}

static void drain_queue_and_assert_tags(uint16_t start_id)
{
    audio_chunk_t chunk = {0};
    uint16_t expected_id = start_id;
    while (audio_chunk_dequeue(&chunk, 0)) {
        TEST_ASSERT_EQUAL_UINT(AUDIO_SOURCE_TAG_BEEP, chunk.tag);
        TEST_ASSERT_EQUAL_UINT16(expected_id, chunk.tag_id);
        expected_id++;
        audio_chunk_release_block(chunk.data);
    }
}

void test_beep_manager_play_should_enqueue_and_callback(void)
{
    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 80,
    };

    beep_request_t req = {
        .duration_ms = 20,
        .freq_hz = 1000.0,
        .amplitude = 1000,
    };

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_init());
    beep_manager_set_done_callback(on_done, &s_done_called);

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &cfg));

    TEST_ASSERT_TRUE(s_done_called);
    TEST_ASSERT_EQUAL(BEEP_STATE_STOPPED, beep_manager_get_state());

    size_t used = audio_descriptor_used();
    TEST_ASSERT_GREATER_THAN_UINT(0, used);

    drain_queue_and_assert_tags(0);
}

void test_beep_manager_tag_id_should_roll_across_plays(void)
{
    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_16K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .volume = 50,
    };

    beep_request_t req = {
        .duration_ms = 10,
        .freq_hz = 800.0,
        .amplitude = 800,
    };

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_init());

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &cfg));
    drain_queue_and_assert_tags(0);

    s_done_called = false;
    beep_manager_set_done_callback(on_done, &s_done_called);

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &cfg));
    TEST_ASSERT_TRUE(s_done_called);
    drain_queue_and_assert_tags(1);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_beep_manager_play_should_enqueue_and_callback);
    RUN_TEST(test_beep_manager_tag_id_should_roll_across_plays);
    return UNITY_END();
}
