/**
 * Audio queue: pooled 1 KiB blocks with descriptor queue for zero-copy handoff.
 *
 * This module manages a fixed-size pool of 1 KiB blocks stored in DRAM
 * and a queue of lightweight descriptors (`audio_chunk_t`) that reference
 * those blocks. Producers enqueue byte slices with an associated source tag;
 * consumers dequeue descriptors, copy or play out the data, and release the
 * block back to the pool.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "esp_err.h"

/* Single audio descriptor queue backed by a fixed pool of 1 KiB blocks. Keep
 * the pool modest (32 blocks) so DRAM-only targets like ESP32-WROOM still
 * have headroom for stacks, Wi-Fi/BT, and audio processing while giving
 * A2DP-only output moderate headroom. */
#define AUDIO_CHUNK_BLOCK_BYTES 1024U
/* Pool depth set to 32 blocks to balance BT heap use against burst tolerance
 * when multiple producers share the queue. */
#define AUDIO_CHUNK_POOL_BLOCKS 32U

typedef enum {
	AUDIO_SOURCE_TAG_INVALID = 0,
	AUDIO_SOURCE_TAG_WAV     = 1,
	AUDIO_SOURCE_TAG_CAPTURE = 2,
	AUDIO_SOURCE_TAG_SYNTH   = 3,
	AUDIO_SOURCE_TAG_BEEP    = 4,
} audio_source_tag_t;

typedef struct {
	uint8_t *data;             /* pointer into the pooled 1 KiB block */
	size_t len;                /* valid byte count in the block */
	audio_source_tag_t tag;    /* source tag */
	uint16_t tag_id;           /* monotonic tag id for diagnostics */
} audio_chunk_t;

/* Initialize descriptor queue and free-block pool. Returns false on OOM. */
bool audio_chunk_pool_init(void);

/* Tear down queues (blocks are static storage and do not need freeing). */
void audio_chunk_pool_deinit(void);

/* Obtain a free 1 KiB block from the pool. Waits up to wait_ticks. */
uint8_t *audio_chunk_alloc_block(TickType_t wait_ticks);

/* Return a previously allocated block to the free pool. */
void audio_chunk_release_block(uint8_t *ptr);

/* Enqueue a copy of `len` bytes (clamped to block size) with a source tag. */
bool audio_chunk_enqueue_bytes(const uint8_t *data, size_t len, audio_source_tag_t tag);
bool audio_chunk_enqueue_block(uint8_t *block, size_t len, audio_source_tag_t tag);

/* Enqueue `len` bytes with an explicit tag_id (diagnostic/flow tracking). */
bool audio_chunk_enqueue_bytes_with_id(const uint8_t *data, size_t len, audio_source_tag_t tag, uint16_t tag_id);

/* Dequeue a descriptor; fills out_chunk on success. wait_ticks bounds wait. */
bool audio_chunk_dequeue(audio_chunk_t *out_chunk, TickType_t wait_ticks);

/* Drain all queued descriptors and return their blocks to the free pool. */
void audio_chunk_clear(void);

/* Number of descriptors currently queued. */
size_t audio_descriptor_used(void);

/* Temporarily block non-beep enqueues so a beep can own the queue. */
void audio_queue_beep_exclusive_begin(void);
void audio_queue_beep_exclusive_end(void);

/* Snapshot queued descriptors without disturbing ordering. Copies up to
 * max_items entries into `out` and leaves the queue intact. Returns
 * ESP_OK on success, ESP_ERR_INVALID_STATE if the queue is uninitialized,
 * or ESP_ERR_NO_MEM if a temporary queue cannot be allocated. */
esp_err_t audio_descriptor_snapshot(audio_chunk_t *out, size_t max_items, size_t *captured_out);
