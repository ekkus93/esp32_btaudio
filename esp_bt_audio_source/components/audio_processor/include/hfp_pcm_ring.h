#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Caller-owned, fixed-capacity SPSC byte ring for HFP PCM.
 *
 * Contract:
 * - Exactly one producer calls hfp_pcm_ring_write_frame().
 * - Exactly one consumer calls hfp_pcm_ring_read().
 * - Storage and the ring object outlive all producer/consumer access.
 * - Reset is permitted only after both endpoints are stopped.
 * - No operation allocates, blocks, overwrites unread bytes, or silently
 *   changes storage.
 */

typedef struct {
    atomic_uint_least32_t sequence;
    atomic_uint_least32_t low;
    atomic_uint_least32_t high;
} hfp_pcm_counter64_t;

typedef struct {
    uint8_t *storage;
    size_t capacity;
    atomic_uint_least32_t generation;
    atomic_uint_least32_t head;
    atomic_uint_least32_t tail;
    atomic_uint_least32_t peak_used;
    atomic_bool initialized;

    /* Producer-owned counters. */
    hfp_pcm_counter64_t total_written_bytes;
    hfp_pcm_counter64_t overflow_frames;
    hfp_pcm_counter64_t overflow_bytes;
    hfp_pcm_counter64_t stale_write_operations;
    hfp_pcm_counter64_t invalid_write_operations;

    /* Consumer-owned counters. */
    hfp_pcm_counter64_t total_read_bytes;
    hfp_pcm_counter64_t underflow_events;
    hfp_pcm_counter64_t underflow_bytes;
    hfp_pcm_counter64_t stale_read_operations;
    hfp_pcm_counter64_t invalid_read_operations;
} hfp_pcm_ring_t;

typedef struct {
    bool initialized;
    uint32_t generation;
    size_t capacity;
    size_t current_used;
    size_t current_free;
    size_t peak_used;
    uint64_t total_written_bytes;
    uint64_t total_read_bytes;
    uint64_t overflow_frames;
    uint64_t overflow_bytes;
    uint64_t underflow_events;
    uint64_t underflow_bytes;
    uint64_t stale_generation_operations;
    uint64_t invalid_operations;
} hfp_pcm_ring_snapshot_t;

esp_err_t hfp_pcm_ring_init(hfp_pcm_ring_t *ring,
                            uint8_t *storage,
                            size_t capacity,
                            uint32_t generation);

bool hfp_pcm_ring_write_frame(hfp_pcm_ring_t *ring,
                              const void *src,
                              size_t frame_bytes,
                              uint32_t generation);

size_t hfp_pcm_ring_read(hfp_pcm_ring_t *ring,
                         void *dst,
                         size_t requested_bytes,
                         uint32_t generation);

esp_err_t hfp_pcm_ring_reset(hfp_pcm_ring_t *ring,
                             uint32_t new_generation,
                             bool endpoints_stopped);

esp_err_t hfp_pcm_ring_get_snapshot(const hfp_pcm_ring_t *ring,
                                    hfp_pcm_ring_snapshot_t *out);

#ifdef __cplusplus
}
#endif
