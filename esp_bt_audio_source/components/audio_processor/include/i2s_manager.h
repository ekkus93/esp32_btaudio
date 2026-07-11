/**
 * I2S manager: reads I2S samples, converts/resamples as needed, and provides
 * direct buffer fills for the ring-buffer audio engine.
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

/**
 * DBG-I2SCAP diagnostic: force-enable the RX channel (if not already) and
 * perform ONE blocking i2s_channel_read with the given timeout, to tell a
 * starving short-timeout read apart from a peripheral that never samples.
 * Restores the prior enable state. Returns the esp_err_t from the read
 * (ESP_ERR_INVALID_STATE if I2S is not configured).
 *
 * @param timeout_ms   blocking read timeout in milliseconds
 * @param out_bytes    [out] bytes actually read (may be NULL)
 * @param sample       [out] first bytes of captured data (may be NULL)
 * @param sample_cap   capacity of @p sample
 * @param out_sample   [out] bytes copied into @p sample (may be NULL)
 */
esp_err_t i2s_manager_rxtest(uint32_t timeout_ms, size_t *out_bytes,
                             uint8_t *sample, size_t sample_cap, size_t *out_sample);

#ifdef CONFIG_BT_MOCK_TESTING
/* Test helper to inject mock frames without real I2S. */
esp_err_t i2s_manager_mock_push(const uint8_t *data,
								size_t len,
								audio_bit_depth_t bit_depth,
								audio_sample_rate_t sample_rate);

/* Expose the mock queue handle for host-side inspection in tests. */
QueueHandle_t i2s_manager_get_mock_queue(void);
#endif
