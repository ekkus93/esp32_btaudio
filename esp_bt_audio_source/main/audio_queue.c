/**
 * Audio queue: pooled 1 KiB blocks + descriptor queue for zero-copy handoff.
 */

#include "audio_queue.h"

#include <stdatomic.h>
#include <string.h>

#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "audio_queue";

static QueueHandle_t s_audio_queue = NULL;          /* holds audio_chunk_t descriptors */
static QueueHandle_t s_audio_block_free = NULL;     /* holds uint8_t* block pointers */
static uint8_t s_audio_block_pool[AUDIO_CHUNK_POOL_BLOCKS][AUDIO_CHUNK_BLOCK_BYTES];
static _Atomic uint16_t s_tag_counter = 0;

bool audio_chunk_pool_init(void)
{
	if (s_audio_queue != NULL && s_audio_block_free != NULL) {
		return true;
	}

	s_audio_queue = xQueueCreate(AUDIO_CHUNK_POOL_BLOCKS, sizeof(audio_chunk_t));
	s_audio_block_free = xQueueCreate(AUDIO_CHUNK_POOL_BLOCKS, sizeof(uint8_t *));

	if (s_audio_queue == NULL || s_audio_block_free == NULL) {
		audio_chunk_pool_deinit();
		ESP_LOGE(TAG, "audio_chunk_pool_init: failed to create queues");
		return false;
	}

	for (size_t i = 0; i < AUDIO_CHUNK_POOL_BLOCKS; ++i) {
		uint8_t *ptr = s_audio_block_pool[i];
		if (xQueueSend(s_audio_block_free, &ptr, 0) != pdTRUE) {
			ESP_LOGE(TAG, "audio_chunk_pool_init: failed to seed free pool at i=%zu", i);
			audio_chunk_pool_deinit();
			return false;
		}
	}

	__atomic_store_n(&s_tag_counter, 0, __ATOMIC_SEQ_CST);
	ESP_LOGI(TAG, "audio_chunk_pool_init: queue ready (%u blocks)", (unsigned)AUDIO_CHUNK_POOL_BLOCKS);
	return true;
}

void audio_chunk_pool_deinit(void)
{
	if (s_audio_queue != NULL) {
		vQueueDelete(s_audio_queue);
		s_audio_queue = NULL;
	}
	if (s_audio_block_free != NULL) {
		vQueueDelete(s_audio_block_free);
		s_audio_block_free = NULL;
	}
}

uint8_t *audio_chunk_alloc_block(TickType_t wait_ticks)
{
	if (s_audio_block_free == NULL) {
		return NULL;
	}
	uint8_t *ptr = NULL;
	if (xQueueReceive(s_audio_block_free, &ptr, wait_ticks) != pdTRUE) {
		return NULL;
	}
	return ptr;
}

void audio_chunk_release_block(uint8_t *ptr)
{
	if (ptr == NULL || s_audio_block_free == NULL) {
		return;
	}
	if (xQueueSend(s_audio_block_free, &ptr, 0) != pdTRUE) {
		ESP_LOGW(TAG, "audio_chunk_release_block: free queue full, dropping block %p", ptr);
	}
}

bool audio_chunk_enqueue_bytes(const uint8_t *data, size_t len, audio_source_tag_t tag)
{
	if (data == NULL || len == 0 || s_audio_queue == NULL) {
		return false;
	}

	const TickType_t wait_ticks = pdMS_TO_TICKS(5);
	uint8_t *block = audio_chunk_alloc_block(wait_ticks);
	if (block == NULL) {
		ESP_LOGW(TAG, "audio_chunk_enqueue_bytes: no free blocks len=%u tag=%d", (unsigned)len, (int)tag);
		return false;
	}

	size_t copy_len = (len > AUDIO_CHUNK_BLOCK_BYTES) ? AUDIO_CHUNK_BLOCK_BYTES : len;
	memcpy(block, data, copy_len);

	audio_chunk_t chunk = {
		.data = block,
		.len = copy_len,
		.tag = tag,
		.tag_id = __atomic_fetch_add(&s_tag_counter, 1, __ATOMIC_SEQ_CST),
	};

	if (xQueueSend(s_audio_queue, &chunk, wait_ticks) != pdTRUE) {
		audio_chunk_release_block(block);
		ESP_LOGW(TAG, "audio_chunk_enqueue_bytes: queue full len=%u tag=%d", (unsigned)copy_len, (int)tag);
		return false;
	}

	return true;
}

bool audio_chunk_dequeue(audio_chunk_t *out_chunk, TickType_t wait_ticks)
{
	if (out_chunk == NULL || s_audio_queue == NULL) {
		return false;
	}
	if (xQueueReceive(s_audio_queue, out_chunk, wait_ticks) != pdTRUE) {
		return false;
	}
	return true;
}

size_t audio_descriptor_used(void)
{
	if (s_audio_queue == NULL) {
		return 0;
	}
	return (size_t)uxQueueMessagesWaiting(s_audio_queue);
}
