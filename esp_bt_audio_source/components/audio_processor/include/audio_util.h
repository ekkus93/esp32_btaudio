#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "audio_processor.h"

/* Shared audio conversion helpers used by i2s_manager and synth. */
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

/**
 * Audio-engine policy: should this wake "hold" (produce nothing more) instead
 * of stuffing a silence chunk when the active source produced 0 bytes?
 *
 * WHY: a live I2S capture delivers at exactly real-time rate, so produced==0
 * only means the DMA is mid-accumulation — the real samples arrive on the next
 * tick. Stuffing silence in that gap interleaved ~60% zeros into the A2DP
 * stream (measured via laptop capture — harsh chop). For every other source
 * (or when I2S isn't running) produced==0 is a genuine underrun and silence is
 * the correct keep-the-ring-fed fallback.
 *
 * Pure/enum-independent so it is host-testable without the private
 * audio_source_t enum. Returns true iff (produced == 0 && is_i2s_source &&
 * i2s_running).
 */
bool audio_engine_hold_for_live_i2s(size_t produced, bool is_i2s_source, bool i2s_running);
