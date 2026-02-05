/**
 * @file audio_ringbuffer.h
 * @brief SPSC (Single Producer Single Consumer) ring buffer for PCM audio
 *
 * WHY: Decouple audio production (audio engine task) from consumption (A2DP callback)
 *      - Old queue-based design had multiple producers → race conditions, complexity
 *      - Ring buffer provides simple, efficient buffering with single writer
 *      - SPSC pattern eliminates most synchronization overhead
 *
 * HOW: Classic circular buffer with head/tail/used_bytes tracking
 *      - Producer writes at head, advances head
 *      - Consumer reads from tail, advances tail
 *      - used_bytes disambiguates full vs empty (both have head == tail)
 *      - Wrap-around handled via modulo arithmetic
 *      - Critical sections very short (just pointer updates)
 *
 * CORRECTNESS: Thread-safety via short critical sections
 *              - portENTER_CRITICAL protects state updates
 *              - No blocking operations (returns immediately if insufficient space/data)
 *              - A2DP callback safe (never blocks, bounded execution time)
 *              - PSRAM option for large buffers (128KB+) without DRAM pressure
 *
 * USAGE PATTERN:
 *   Producer (audio engine task):
 *     while (running) {
 *         uint8_t chunk[1024];
 *         size_t produced = generate_audio(chunk, sizeof(chunk));
 *         audio_rb_write(rb, chunk, produced);
 *     }
 *
 *   Consumer (A2DP callback):
 *     size_t read = audio_rb_read(rb, dst_buffer, bytes_needed);
 *     if (read < bytes_needed) {
 *         // Underrun: zero-fill remainder
 *         memset(dst_buffer + read, 0, bytes_needed - read);
 *     }
 *
 * CODE_REVIEW6 Phase 1, Task 1.1
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque ring buffer handle (internal structure hidden)
 */
typedef struct audio_rb audio_rb_t;

/**
 * Initialize ring buffer
 *
 * Allocates ring buffer structure and backing buffer (DRAM or PSRAM).
 * Must be called before any read/write operations.
 *
 * @param[out] rb           Pointer to ring buffer handle (will be allocated)
 * @param[in]  capacity     Buffer capacity in bytes (will be rounded to alignment)
 * @param[in]  use_psram    true = allocate in PSRAM (if available), false = DRAM
 * @return ESP_OK on success
 *         ESP_ERR_NO_MEM if allocation fails
 *         ESP_ERR_INVALID_ARG if rb is NULL or capacity is 0
 */
esp_err_t audio_rb_init(audio_rb_t **rb, size_t capacity_bytes, bool use_psram);

/**
 * Deinitialize ring buffer
 *
 * Frees all memory associated with ring buffer. Safe to call with NULL.
 * After this call, rb is invalid and must not be used.
 *
 * @param[in] rb  Ring buffer handle (may be NULL)
 */
void audio_rb_deinit(audio_rb_t *rb);

/**
 * Write data to ring buffer (producer API)
 *
 * CRITICAL: Only one thread should call this (audio engine task).
 * Writes up to len bytes into ring buffer. Returns immediately if insufficient
 * space available (non-blocking).
 *
 * Thread-safety: Uses short critical section for state update.
 * A2DP callback safety: Never blocks, bounded execution time.
 *
 * @param[in] rb   Ring buffer handle
 * @param[in] src  Source data buffer
 * @param[in] len  Number of bytes to write
 * @return Number of bytes actually written (0 to len)
 *         May be less than len if insufficient space available
 *         Returns 0 if rb is NULL, src is NULL, or len is 0
 */
size_t audio_rb_write(audio_rb_t *rb, const uint8_t *src, size_t len);

/**
 * Read data from ring buffer (consumer API)
 *
 * CRITICAL: Only one thread should call this (A2DP callback task).
 * Reads up to len bytes from ring buffer. Returns immediately if insufficient
 * data available (non-blocking).
 *
 * Thread-safety: Uses short critical section for state update.
 * A2DP callback safety: Never blocks, bounded execution time.
 *
 * @param[in]  rb   Ring buffer handle
 * @param[out] dst  Destination buffer
 * @param[in]  len  Number of bytes to read
 * @return Number of bytes actually read (0 to len)
 *         May be less than len if insufficient data available
 *         Returns 0 if rb is NULL, dst is NULL, or len is 0
 */
size_t audio_rb_read(audio_rb_t *rb, uint8_t *dst, size_t len);

/**
 * Query bytes available to read
 *
 * Non-destructive query (doesn't consume data).
 * Snapshot of current state (may change immediately after return in SPSC scenario).
 *
 * @param[in] rb  Ring buffer handle
 * @return Number of bytes available to read (0 if empty or rb is NULL)
 */
size_t audio_rb_available_to_read(const audio_rb_t *rb);

/**
 * Query bytes available to write
 *
 * Non-destructive query (doesn't produce data).
 * Snapshot of current state (may change immediately after return in SPSC scenario).
 *
 * @param[in] rb  Ring buffer handle
 * @return Number of bytes available to write (0 if full or rb is NULL)
 */
size_t audio_rb_available_to_write(const audio_rb_t *rb);

/**
 * Query total ring buffer capacity
 *
 * Returns the capacity specified during init (may be rounded for alignment).
 *
 * @param[in] rb  Ring buffer handle
 * @return Total capacity in bytes (0 if rb is NULL)
 */
size_t audio_rb_capacity(const audio_rb_t *rb);

/**
 * Query peak ring buffer usage
 *
 * Tracks the maximum number of bytes that have been in the buffer since
 * init or last reset_stats. Useful for tuning capacity.
 *
 * @param[in] rb  Ring buffer handle
 * @return Peak bytes used (0 if rb is NULL)
 */
size_t audio_rb_peak_used(const audio_rb_t *rb);

/**
 * Reset ring buffer statistics
 *
 * Resets peak usage tracking. Does NOT clear buffered data or reset head/tail.
 *
 * @param[in] rb  Ring buffer handle
 */
void audio_rb_reset_stats(audio_rb_t *rb);

#ifdef __cplusplus
}
#endif
