/**
 * WAV playback manager: parses WAV headers, streams audio via audio_queue,
 * and tracks playback state without relying on ringbuffers.
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "audio_processor.h"  /* for audio_config_t, audio bit depths/rates */

typedef struct {
	uint8_t *proc_buf;   /* conversion buffer */
	uint8_t *proc_buf2;  /* resample/output buffer */
	size_t work_bytes;   /* size of each buffer */
} play_manager_buffers_t;

esp_err_t play_manager_init(const audio_config_t *config,
			const play_manager_buffers_t *buffers);
void play_manager_deinit(void);

esp_err_t play_manager_play_wav(const char *path);
void play_manager_abort(bool allow_resume);
bool play_manager_is_active(void);

/* Attempt to enqueue more audio from the active WAV. Call from worker/reader
 * context; non-blocking except for short file I/O and conversion.
 */
esp_err_t play_manager_fill(void);

/* Notify the manager that `bytes` were consumed from the queue. Returns true
 * when playback has no more pending bytes.
 */
bool play_manager_consume(size_t bytes);

size_t play_manager_pending_bytes(void);

#ifdef CONFIG_BT_MOCK_TESTING
/* Test hooks for visibility and small overrides. */
void play_manager_test_set_frame_bytes_dst(size_t frame_bytes);
size_t play_manager_test_residual_bytes(void);
#endif
