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

/* Single audio descriptor queue backed by a fixed pool of 1 KiB blocks. */
#define AUDIO_CHUNK_BLOCK_BYTES 1024U
#define AUDIO_CHUNK_POOL_BLOCKS 128U

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

/* Enqueue `len` bytes with an explicit tag_id (diagnostic/flow tracking). */
bool audio_chunk_enqueue_bytes_with_id(const uint8_t *data, size_t len, audio_source_tag_t tag, uint16_t tag_id);

/* Dequeue a descriptor; fills out_chunk on success. wait_ticks bounds wait. */
bool audio_chunk_dequeue(audio_chunk_t *out_chunk, TickType_t wait_ticks);

/* Number of descriptors currently queued. */
size_t audio_descriptor_used(void);
