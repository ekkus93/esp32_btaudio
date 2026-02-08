/**
 * @file audio_span_log.h
 * @brief Span log for audio engine metadata tracking (debugging)
 *
 * WHY: Provides visibility into audio engine behavior for debugging
 *      - Track which source is active at any time
 *      - Monitor ring buffer occupancy over time
 *      - Identify beep overlays and source switches
 *      - NOT position-coupled to audio (avoids desync bugs from CODE_REVIEW4)
 *
 * HOW: Small append-only circular buffer of metadata entries
 *      - Each span records: seq, timestamp, source, bytes written, ring state
 *      - Wraps when full (keeps most recent N entries)
 *      - Query returns last N spans for timeline reconstruction
 *
 * CORRECTNESS: Thread-safe via short critical sections
 *              - Append is non-blocking (returns immediately if full)
 *              - Query copies entries atomically
 *              - Minimal overhead (suitable for production debug builds)
 *
 * USAGE PATTERN:
 *   Producer (audio engine task):
 *     size_t written = audio_rb_write(rb, chunk, produced);
 *     span_log_push(seq++, esp_timer_get_time()/1000, offset, written, 
 *                   ring_used, AUDIO_SOURCE_I2S, beep ? SPAN_FLAG_BEEP : 0);
 *
 *   Debugging:
 *     audio_rb_span_t spans[20];
 *     size_t actual = 0;
 *     span_log_get_last_n(spans, 20, &actual);
 *     for (size_t i = 0; i < actual; i++) {
 *         printf("seq=%u ts=%u src=%u bytes=%zu\n", 
 *                spans[i].seq, spans[i].timestamp_ms, spans[i].source, spans[i].bytes);
 *     }
 *
 * CODE_REVIEW6 Phase 4, Task 4.1
 */

#ifndef AUDIO_SPAN_LOG_H
#define AUDIO_SPAN_LOG_H

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Span flags (bit field)
 */
#define SPAN_FLAG_BEEP_OVERLAY  (1U << 0)  /* Beep was mixed in this span */
#define SPAN_FLAG_SOURCE_SWITCH (1U << 1)  /* Source changed in this span */
#define SPAN_FLAG_UNDERRUN      (1U << 2)  /* Consumer underran ring buffer */
#define SPAN_FLAG_PAUSED        (1U << 3)  /* Engine was paused (watermark) */

/**
 * Audio source types (matches audio_processor.c enum)
 */
#define AUDIO_SPAN_SOURCE_I2S      0  /* Historical: was WAV (0), now I2S is primary */
#define AUDIO_SPAN_SOURCE_SYNTH    1
#define AUDIO_SPAN_SOURCE_SILENCE  2

/**
 * Span log entry
 *
 * Records metadata about one audio engine write operation.
 * Kept small (16 bytes) to minimize memory usage.
 */
typedef struct {
    uint32_t seq;               /**< Monotonic sequence number */
    uint32_t timestamp_ms;      /**< Timestamp when span was written (milliseconds) */
    size_t   bytes;             /**< Bytes written in this span */
    uint16_t ring_used_kb;      /**< Ring buffer occupancy after write (KB, for compactness) */
    uint8_t  source;            /**< Audio source (AUDIO_SPAN_SOURCE_*) */
    uint8_t  flags;             /**< Span flags (SPAN_FLAG_*) */
} audio_rb_span_t;

/**
 * Initialize span log
 *
 * Allocates circular buffer for span entries. Must be called before
 * span_log_push() or span_log_get_last_n().
 *
 * @param[in] max_entries  Maximum number of span entries to store (default: 256)
 * @return true on success, false if already initialized or allocation failed
 */
bool span_log_init(size_t max_entries);

/**
 * Deinitialize span log
 *
 * Frees all memory. Safe to call multiple times.
 */
void span_log_deinit(void);

/**
 * Append span entry to log
 *
 * Records metadata about an audio engine write. Thread-safe (uses critical section).
 * Non-blocking: if log is full, oldest entry is overwritten.
 *
 * @param[in] seq             Monotonic sequence number (audio engine maintains this)
 * @param[in] timestamp_ms    Timestamp in milliseconds (e.g., esp_timer_get_time()/1000)
 * @param[in] bytes           Bytes written to ring buffer
 * @param[in] ring_used       Ring buffer occupancy after write (bytes)
 * @param[in] source          Audio source (AUDIO_SPAN_SOURCE_*)
 * @param[in] flags           Span flags (SPAN_FLAG_*)
 */
void span_log_push(uint32_t seq, uint32_t timestamp_ms, size_t bytes,
                   size_t ring_used, uint8_t source, uint8_t flags);

/**
 * Get last N span entries
 *
 * Copies most recent span entries into caller's buffer. Thread-safe.
 * Entries are returned in chronological order (oldest first).
 *
 * @param[out] out       Destination buffer (must hold at least n entries)
 * @param[in]  n         Maximum number of entries to retrieve
 * @param[out] actual    Number of entries actually retrieved (may be less than n)
 *                       If NULL, count is not returned
 * @return true on success, false if span log not initialized or out is NULL
 */
bool span_log_get_last_n(audio_rb_span_t *out, size_t n, size_t *actual);

/**
 * Reset span log (clear all entries)
 *
 * Useful for starting fresh after warmup or configuration change.
 * Thread-safe.
 */
void span_log_reset(void);

/**
 * Get span log capacity
 *
 * @return Maximum number of entries span log can hold (0 if not initialized)
 */
size_t span_log_capacity(void);

/**
 * Get current span log count
 *
 * @return Number of valid entries currently in span log (0 if not initialized)
 */
size_t span_log_count(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_SPAN_LOG_H */
