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

/* Directly process a captured frame (used by tests and mock feeds). */
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
