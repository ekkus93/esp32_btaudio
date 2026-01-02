#include "unity.h"
#include "unity_test_runner.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "audio_queue.h"

static void reset_queue(void)
{
    audio_chunk_pool_deinit();
    TEST_ASSERT_TRUE(audio_chunk_pool_init());
}

TEST_CASE("audio_queue_enqueue_and_dequeue_single", "[audio_queue]")
{
    reset_queue();

    const uint8_t payload[] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(payload, sizeof(payload), AUDIO_SOURCE_TAG_WAV));

    audio_chunk_t chunk = {0};
    TEST_ASSERT_TRUE(audio_chunk_dequeue(&chunk, pdMS_TO_TICKS(10)));
    TEST_ASSERT_EQUAL(sizeof(payload), chunk.len);
    TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_WAV, chunk.tag);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, chunk.data, chunk.len);

    audio_chunk_release_block(chunk.data);
    TEST_ASSERT_EQUAL(0, audio_descriptor_used());

    audio_chunk_pool_deinit();
}

TEST_CASE("audio_queue_dequeue_times_out_when_empty", "[audio_queue]")
{
    reset_queue();

    audio_chunk_t chunk = {0};
    TEST_ASSERT_FALSE(audio_chunk_dequeue(&chunk, pdMS_TO_TICKS(1)));
    TEST_ASSERT_EQUAL(0, audio_descriptor_used());

    audio_chunk_pool_deinit();
}

void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
}

TEST_CASE("audio_queue_reuses_blocks", "[audio_queue]")
{
    reset_queue();

    uint8_t payload[32];
    for (size_t i = 0; i < 3; ++i) {
        memset(payload, (int)(i + 1), sizeof(payload));
        TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(payload, sizeof(payload), AUDIO_SOURCE_TAG_BEEP));

        audio_chunk_t chunk = {0};
        TEST_ASSERT_TRUE(audio_chunk_dequeue(&chunk, pdMS_TO_TICKS(10)));
        TEST_ASSERT_EQUAL(sizeof(payload), chunk.len);
        TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_BEEP, chunk.tag);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, chunk.data, chunk.len);
        audio_chunk_release_block(chunk.data);
    }

    TEST_ASSERT_EQUAL(0, audio_descriptor_used());

    audio_chunk_pool_deinit();
}

TEST_CASE("audio_queue_clamps_to_block_size", "[audio_queue]")
{
    reset_queue();

    uint8_t payload[AUDIO_CHUNK_BLOCK_BYTES + 64];
    for (size_t i = 0; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)(i & 0xFF);
    }

    TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(payload, sizeof(payload), AUDIO_SOURCE_TAG_CAPTURE));

    audio_chunk_t chunk = {0};
    TEST_ASSERT_TRUE(audio_chunk_dequeue(&chunk, pdMS_TO_TICKS(10)));
    TEST_ASSERT_EQUAL(AUDIO_CHUNK_BLOCK_BYTES, chunk.len);
    TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_CAPTURE, chunk.tag);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, chunk.data, chunk.len);

    audio_chunk_release_block(chunk.data);
    TEST_ASSERT_EQUAL(0, audio_descriptor_used());

    audio_chunk_pool_deinit();
}

TEST_CASE("audio_queue_alloc_exhausts_and_recovers", "[audio_queue]")
{
    reset_queue();

    uint8_t *blocks[AUDIO_CHUNK_POOL_BLOCKS] = {0};

    for (size_t i = 0; i < AUDIO_CHUNK_POOL_BLOCKS; ++i) {
        blocks[i] = audio_chunk_alloc_block(0);
        TEST_ASSERT_NOT_NULL_MESSAGE(blocks[i], "expected free block while pool not exhausted");
    }

    uint8_t *extra = audio_chunk_alloc_block(0);
    TEST_ASSERT_NULL_MESSAGE(extra, "pool should be exhausted after allocating all blocks");

    for (size_t i = 0; i < AUDIO_CHUNK_POOL_BLOCKS; ++i) {
        audio_chunk_release_block(blocks[i]);
    }

    uint8_t *recovered = audio_chunk_alloc_block(0);
    TEST_ASSERT_NOT_NULL_MESSAGE(recovered, "pool should recover after releasing blocks");
    audio_chunk_release_block(recovered);

    audio_chunk_pool_deinit();
}

TEST_CASE("audio_queue_enqueue_fails_when_full", "[audio_queue]")
{
    reset_queue();

    uint8_t payload[8] = {0xAA};

    for (size_t i = 0; i < AUDIO_CHUNK_POOL_BLOCKS; ++i) {
        TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(payload, sizeof(payload), AUDIO_SOURCE_TAG_SYNTH));
    }

    TEST_ASSERT_EQUAL(AUDIO_CHUNK_POOL_BLOCKS, audio_descriptor_used());

    TEST_ASSERT_FALSE_MESSAGE(audio_chunk_enqueue_bytes(payload, sizeof(payload), AUDIO_SOURCE_TAG_SYNTH),
                              "enqueue should fail when descriptor queue is full");

    for (size_t i = 0; i < AUDIO_CHUNK_POOL_BLOCKS; ++i) {
        audio_chunk_t chunk = {0};
        TEST_ASSERT_TRUE(audio_chunk_dequeue(&chunk, pdMS_TO_TICKS(10)));
        audio_chunk_release_block(chunk.data);
    }

    TEST_ASSERT_EQUAL(0, audio_descriptor_used());

    audio_chunk_pool_deinit();
}

TEST_CASE("audio_queue_clear_returns_blocks", "[audio_queue]")
{
    reset_queue();

    uint8_t payload[16] = {0xBB};
    TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(payload, sizeof(payload), AUDIO_SOURCE_TAG_WAV));
    TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(payload, sizeof(payload), AUDIO_SOURCE_TAG_BEEP));
    TEST_ASSERT_EQUAL(2, audio_descriptor_used());

    audio_chunk_clear();
    TEST_ASSERT_EQUAL(0, audio_descriptor_used());

    /* All blocks should be reusable after clear */
    TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(payload, sizeof(payload), AUDIO_SOURCE_TAG_SYNTH));
    audio_chunk_t chunk = {0};
    TEST_ASSERT_TRUE(audio_chunk_dequeue(&chunk, pdMS_TO_TICKS(10)));
    audio_chunk_release_block(chunk.data);
    TEST_ASSERT_EQUAL(0, audio_descriptor_used());

    audio_chunk_pool_deinit();
}

TEST_CASE("audio_descriptor_snapshot_validates_args_and_state", "[audio_queue]")
{
    audio_chunk_t snapshot[2] = {0};
    size_t captured = 0;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_descriptor_snapshot(NULL, 2, &captured));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_descriptor_snapshot(snapshot, 0, &captured));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, audio_descriptor_snapshot(snapshot, 2, &captured));

    reset_queue();

    const uint8_t a[] = {0x11};
    const uint8_t b[] = {0x22};
    const uint8_t c[] = {0x33};
    TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(a, sizeof(a), AUDIO_SOURCE_TAG_WAV));
    TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(b, sizeof(b), AUDIO_SOURCE_TAG_BEEP));
    TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(c, sizeof(c), AUDIO_SOURCE_TAG_SYNTH));

    TEST_ASSERT_EQUAL(ESP_OK, audio_descriptor_snapshot(snapshot, 2, &captured));
    TEST_ASSERT_EQUAL(2, captured);
    TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_WAV, snapshot[0].tag);
    TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_BEEP, snapshot[1].tag);
    TEST_ASSERT_EQUAL(3, audio_descriptor_used());

    /* Queue order should remain intact after snapshot */
    audio_chunk_t chunk = {0};
    TEST_ASSERT_TRUE(audio_chunk_dequeue(&chunk, pdMS_TO_TICKS(10)));
    TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_WAV, chunk.tag);
    audio_chunk_release_block(chunk.data);

    audio_chunk_pool_deinit();
}
