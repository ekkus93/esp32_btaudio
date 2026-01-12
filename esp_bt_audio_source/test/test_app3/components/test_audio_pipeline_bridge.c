/* Device-side audio_queue coverage using the production audio_queue module. */

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

static void test_audio_queue_init_and_enqueue_dequeue(void)
{
	TEST_ASSERT_TRUE(audio_chunk_pool_init());

	const uint8_t payload[6] = {1, 2, 3, 4, 5, 6};
	TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(payload, sizeof(payload), AUDIO_SOURCE_TAG_CAPTURE));

	audio_chunk_t out = {0};
	TEST_ASSERT_TRUE(audio_chunk_dequeue(&out, pdMS_TO_TICKS(10)));
	TEST_ASSERT_EQUAL_size_t(sizeof(payload), out.len);
	TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_CAPTURE, out.tag);
	TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, out.data, sizeof(payload));

	audio_chunk_release_block(out.data);
}

static void test_audio_queue_snapshot_preserves_entries(void)
{
	TEST_ASSERT_TRUE(audio_chunk_pool_init());

	uint8_t data1[4] = {10, 11, 12, 13};
	uint8_t data2[2] = {20, 21};
	TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(data1, sizeof(data1), AUDIO_SOURCE_TAG_CAPTURE));
	TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(data2, sizeof(data2), AUDIO_SOURCE_TAG_BEEP));

	audio_chunk_t snapshot[2] = {0};
	size_t captured = 0;
	TEST_ASSERT_EQUAL(ESP_OK, audio_descriptor_snapshot(snapshot, 2, &captured));
	TEST_ASSERT_EQUAL_size_t(2, captured);
	TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_CAPTURE, snapshot[0].tag);
	TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_BEEP, snapshot[1].tag);

	/* Ensure queue still holds both entries for normal dequeue order. */
	audio_chunk_t out = {0};
	TEST_ASSERT_TRUE(audio_chunk_dequeue(&out, 0));
	audio_chunk_release_block(out.data);
	TEST_ASSERT_TRUE(audio_chunk_dequeue(&out, 0));
	audio_chunk_release_block(out.data);
}

static void test_audio_queue_clear_releases_blocks(void)
{
	TEST_ASSERT_TRUE(audio_chunk_pool_init());
	uint8_t data[8] = {0};
	TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(data, sizeof(data), AUDIO_SOURCE_TAG_SYNTH));
	TEST_ASSERT_GREATER_THAN_UINT(0, audio_descriptor_used());

	audio_chunk_clear();
	TEST_ASSERT_EQUAL_UINT(0, audio_descriptor_used());
}

void audio_queue_tests_register(void)
{
	RUN_TEST(test_audio_queue_init_and_enqueue_dequeue);
	RUN_TEST(test_audio_queue_snapshot_preserves_entries);
	RUN_TEST(test_audio_queue_clear_releases_blocks);
}
