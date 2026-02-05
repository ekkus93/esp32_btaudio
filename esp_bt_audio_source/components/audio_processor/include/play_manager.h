/**
 * WAV playback manager: parses WAV headers, streams audio via the ring-buffer
 * engine using a direct fill API, and tracks playback state.
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "audio_processor.h"  /* for audio_config_t, audio bit depths/rates */
#include "audio_util.h"

typedef struct {
	size_t work_bytes;   /* optional workspace sizing hint */
} play_manager_buffers_t;

esp_err_t play_manager_init(const audio_config_t *config,
			const play_manager_buffers_t *buffers);
void play_manager_deinit(void);

esp_err_t play_manager_play_wav(const char *path);
void play_manager_abort(bool allow_resume);
bool play_manager_is_active(void);

/**
 * WAV source fill API for ring buffer architecture (CODE_REVIEW6 Phase 3.1)
 * 
 * Fills buffer from active WAV playback. Uses internal resampler+stash pipeline
 * to produce exactly the requested bytes (or less at EOF).
 * 
 * @param dst Destination buffer (must be at least dst_bytes)
 * @param dst_bytes Requested bytes to fill (typically 1024)
 * @return Actual bytes written (0 if inactive or EOF, dst_bytes if successful)
 * 
 * WHY: Ring buffer architecture needs source "fill" API instead of queue enqueue
 * HOW: Reuses produce_one_output_block() logic, writes to dst instead of enqueueing
 * CORRECTNESS: Thread-safe via existing mutex, never blocks, handles EOF cleanly
 */
size_t wav_source_fill(uint8_t *dst, size_t dst_bytes);

/**
 * WAV playback runtime status for diagnostics (CODE_REVIEW5 Task 2.3)
 */
typedef struct {
    bool active;                       /* WAV playback active */
    const char *filename;              /* Currently playing file (NULL if inactive) */
    audio_sample_rate_t src_rate;      /* Source sample rate (Hz) */
    uint16_t src_channels;             /* Source channels (1=mono, 2=stereo) */
    audio_bit_depth_t src_bit_depth;   /* Source bit depth */
    audio_sample_rate_t dst_rate;      /* Output sample rate (Hz) */
    uint16_t dst_channels;             /* Output channels */
    audio_bit_depth_t dst_bit_depth;   /* Output bit depth */
    size_t src_frames_read;            /* Source frames read so far */
    size_t dst_frames_produced;        /* Destination frames produced so far */
    size_t expected_dst_frames;        /* Expected total destination frames */
    size_t stash_frames;               /* Current PCM stash buffer fill (frames) */
    size_t stash_capacity;             /* PCM stash buffer capacity (frames) */
    uint32_t resampler_pos_q16;        /* Resampler Q16.16 phase position */
} play_manager_status_t;

/**
 * Get current WAV playback status. Safe to call anytime.
 * Returns false if play_manager not initialized.
 * If not active, most fields will be zero/NULL.
 */
bool play_manager_get_status(play_manager_status_t *status);

/**
 * WAV playback instrumentation data for verification and diagnostics.
 * Updated during playback and reset when new playback starts.
 */
typedef struct {
    size_t expected_data_bytes;        /* Expected bytes from WAV data chunk */
    size_t bytes_read_from_file;       /* Actual bytes read from file */
    size_t bytes_produced;             /* Bytes produced via wav_source_fill */
} play_manager_instrumentation_t;

/**
 * Get current instrumentation counters. Safe to call anytime.
 * Returns false if play_manager not initialized.
 */
bool play_manager_get_instrumentation(play_manager_instrumentation_t *instr);

#ifdef CONFIG_BT_MOCK_TESTING
/* Test hooks for visibility and small overrides. */
void play_manager_test_set_frame_bytes_dst(size_t frame_bytes);
void play_manager_test_force_zero_resample(bool enable);
#endif
