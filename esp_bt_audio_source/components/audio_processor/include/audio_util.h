#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "audio_processor.h"

/* Shared audio conversion helpers used by play_manager and i2s_manager. */
typedef struct {
	const void *src;
	void *dst;
	size_t src_size;
	audio_bit_depth_t src_bit_depth;
	audio_bit_depth_t dst_bit_depth;
	size_t *dst_size;
	/* Optional cap for writes; 0 uses src_size as-is. */
	size_t work_bytes;
} audio_convert_args_t;

typedef struct {
	const void *src;
	void *dst;
	size_t src_size;
	audio_sample_rate_t src_rate;
	audio_sample_rate_t dst_rate;
	audio_bit_depth_t bit_depth;
	audio_channel_t channels;
	size_t *dst_size;
	/* Optional cap for writes; 0 means no extra cap. */
	size_t work_bytes;
} audio_resample_args_t;

esp_err_t convert_audio_format(const audio_convert_args_t *args);
esp_err_t resample_audio(const audio_resample_args_t *args);
