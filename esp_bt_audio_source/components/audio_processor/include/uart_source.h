/**
 * uart_source.h — UART audio source for the audio processor
 *
 * Receives stereo 22050 Hz s16le PCM (pushed by the UART reader task),
 * upsamples it 2x to 44100 Hz and serves it to the audio engine as
 * AUDIO_SOURCE_UART.
 *
 * Concurrency contract (mirrors the SPSC staging ring underneath):
 *   - uart_source_write():  UART reader task only (single producer)
 *   - uart_source_fill():   audio engine task only (single consumer)
 *   - start/request_drain/stop: reader-task/command context; stop()
 *     deactivates first and waits >= 2 engine ticks before freeing the
 *     ring so an in-flight fill() completes safely.
 */

#ifndef UART_SOURCE_H
#define UART_SOURCE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UART_SOURCE_STATE_INACTIVE = 0, /**< not started (or fully stopped/drained) */
    UART_SOURCE_STATE_PREBUFFER,    /**< started, buffering until 50% ring fill */
    UART_SOURCE_STATE_ACTIVE,       /**< serving audio to the engine */
    UART_SOURCE_STATE_DRAINING,     /**< STOP seen: playing out remaining data */
} uart_source_state_t;

typedef struct {
    uart_source_state_t state;
    size_t ring_used;         /**< bytes currently buffered (22.05 kHz domain) */
    size_t ring_capacity;     /**< staging ring capacity */
    size_t prebuffer_target;  /**< fill level that flips PREBUFFER -> ACTIVE */
    uint32_t bytes_in;        /**< total payload bytes accepted by write() */
    uint32_t bytes_out;       /**< total upsampled bytes produced by fill() */
    uint32_t underrun_events; /**< fills that zero-padded while ACTIVE */
    uint32_t overflow_events; /**< writes that could not store all bytes */
} uart_source_stats_t;

/**
 * Allocate the staging ring (DRAM) and enter PREBUFFER. Resets stats and
 * the upsampler seed. ESP_ERR_INVALID_STATE if already started,
 * ESP_ERR_INVALID_ARG for ring_bytes == 0, ESP_ERR_NO_MEM on alloc failure.
 */
esp_err_t uart_source_start(size_t ring_bytes);

/**
 * Push received 22050 Hz stereo s16le payload bytes (reader task only).
 * Returns bytes accepted; short/zero return means the ring is full
 * (overflow_events increments). Returns 0 when not started.
 */
size_t uart_source_write(const uint8_t *data, size_t len);

/**
 * Produce dst_bytes of 44100 Hz stereo s16le audio (engine task only).
 * Consumes dst_bytes/2 from the staging ring and upsamples 2x. On
 * shortfall the tail is zero-filled (underrun_events increments unless
 * draining). Returns dst_bytes while ACTIVE/DRAINING, else 0.
 * dst_bytes should be a multiple of 8 (one 22.05k frame -> 8 output bytes).
 */
size_t uart_source_fill(uint8_t *dst, size_t dst_bytes);

/**
 * In-band STOP received: keep serving until the ring is empty, then
 * deactivate. Also activates a short stream still in PREBUFFER so its
 * tail plays out.
 */
void uart_source_request_drain(void);

/**
 * Deactivate immediately and free the ring. Blocks ~10 ms (>= 2 engine
 * ticks) between deactivation and free. Safe to call when not started.
 */
void uart_source_stop(void);

/** True while the engine should select AUDIO_SOURCE_UART (ACTIVE/DRAINING). */
bool uart_source_is_active(void);

uart_source_state_t uart_source_get_state(void);

/** Snapshot stats (out must be non-NULL). */
void uart_source_get_stats(uart_source_stats_t *out);

/**
 * 2x linear upsampler for interleaved stereo s16 PCM (22050 -> 44100 Hz).
 *
 * For each input frame (L,R), emits two output frames per channel:
 * the midpoint (prev + cur) / 2 (C truncation), then cur. prev[2]
 * ({L,R}) carries the last input frame across calls so split feeds are
 * seamless; reset it to the first sample value (or zero) at stream start.
 *
 * in:  in_frames stereo frames (2*in_frames int16 samples)
 * out: must hold 2*in_frames stereo frames (4*in_frames int16 samples)
 */
void audio_upsample2x_s16_stereo(const int16_t *in, size_t in_frames,
                                 int16_t *out, int16_t prev[2]);

#ifdef __cplusplus
}
#endif

#endif /* UART_SOURCE_H */
