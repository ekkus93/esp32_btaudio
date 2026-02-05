/**
 * I2S manager: reads I2S samples, converts/resamples as needed, and enqueues
 * audio descriptors into the shared audio_queue with AUDIO_SOURCE_TAG_CAPTURE.
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"
#include "audio_processor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "audio_util.h"

typedef struct {
	uint8_t *raw_buf;
	size_t raw_buf_bytes;
	uint8_t *proc_buf;
	uint8_t *proc_buf2;
	size_t work_bytes;
} i2s_manager_buffers_t;

esp_err_t i2s_manager_init(const audio_config_t *config, const i2s_manager_buffers_t *buffers);
void i2s_manager_deinit(void);

esp_err_t i2s_manager_start(void);
esp_err_t i2s_manager_stop(void);
bool i2s_manager_is_running(void);

/**
 * I2S source fill API for ring buffer architecture (CODE_REVIEW6 Phase 3.2)
 * 
 * Fills buffer from I2S capture. Reads from I2S DMA, converts format, resamples,
 * and writes directly to destination buffer.
 * 
 * @param dst Destination buffer (must be at least dst_bytes)
 * @param dst_bytes Requested bytes to fill (typically 1024)
 * @return Actual bytes written (0 if not running or no data available)
 * 
 * WHY: Ring buffer architecture needs source "fill" API instead of queue enqueue
 * HOW: Reuses process_frame() logic, writes to dst instead of enqueueing
 * CORRECTNESS: Thread-safe, never blocks, handles format conversion cleanly
 */
size_t i2s_source_fill(uint8_t *dst, size_t dst_bytes);

/* Directly process a captured frame (used by tests and mock feeds).
 * NOTE: Legacy queue-based API - retained for parallel operation during migration (CODE_REVIEW6 Phase 3).
 *       Will be removed in Phase 6 after ring buffer fully validated.
 */
esp_err_t i2s_manager_handle_frame(const uint8_t *data,
								  size_t len,
								  audio_bit_depth_t bit_depth,
								  audio_sample_rate_t sample_rate);

#ifdef CONFIG_BT_MOCK_TESTING
/* Test helper to inject mock frames without real I2S. */
esp_err_t i2s_manager_mock_push(const uint8_t *data,
								size_t len,
								audio_bit_depth_t bit_depth,
								audio_sample_rate_t sample_rate);

/* Expose the mock queue handle for host-side inspection in tests. */
QueueHandle_t i2s_manager_get_mock_queue(void);
#endif
