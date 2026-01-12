#include "unity.h"

#include "audio_queue.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

void setUp(void)
{
    audio_chunk_pool_deinit();
}

void tearDown(void)
{
    audio_chunk_pool_deinit();
}

static void drain_queue(void)
{
    audio_chunk_t chunk = {0};
    while (audio_chunk_dequeue(&chunk, 0)) {
        audio_chunk_release_block(chunk.data);
    }
}

void test_audio_queue_init_and_enqueue(void)
{
    TEST_ASSERT_TRUE(audio_chunk_pool_init());
    TEST_ASSERT_TRUE(audio_chunk_pool_init());

    const uint8_t payload[5] = {1, 2, 3, 4, 5};
    TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(payload, sizeof(payload), AUDIO_SOURCE_TAG_CAPTURE));

    audio_chunk_t out = {0};
    TEST_ASSERT_TRUE(audio_chunk_dequeue(&out, pdMS_TO_TICKS(10)));
    TEST_ASSERT_EQUAL_size_t(sizeof(payload), out.len);
    TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_CAPTURE, out.tag);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, out.data, sizeof(payload));
    audio_chunk_release_block(out.data);
}

void test_audio_queue_snapshot_should_capture_entries(void)
{
    TEST_ASSERT_TRUE(audio_chunk_pool_init());
    uint8_t a[2] = {10, 11};
    uint8_t b[3] = {20, 21, 22};
    TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(a, sizeof(a), AUDIO_SOURCE_TAG_CAPTURE));
    TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(b, sizeof(b), AUDIO_SOURCE_TAG_BEEP));

    audio_chunk_t snap[2] = {0};
    size_t captured = 0;
    TEST_ASSERT_EQUAL(ESP_OK, audio_descriptor_snapshot(snap, 2, &captured));
    TEST_ASSERT_EQUAL_size_t(2, captured);
    TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_CAPTURE, snap[0].tag);
    TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_BEEP, snap[1].tag);

    drain_queue();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_audio_queue_init_and_enqueue);
    RUN_TEST(test_audio_queue_snapshot_should_capture_entries);
    return UNITY_END();
}
