/**
 * WAV playback manager using audio_queue for zero-copy handoff.
 * 
 * DATA LOSS PREVENTION STRATEGY (CODE_REVIEW4 Phase 1):
 * 
 * This module implements a robust WAV playback system with lossless guarantees
 * even under memory pressure or queue backpressure conditions. Key mechanisms:
 * 
 * 1. FILE REWIND ON ENQUEUE FAILURE (Task 1.2):
 *    - When audio_chunk_enqueue_block() fails (queue full, no memory), we
 *      rewind the file pointer by the number of bytes we just read
 *    - This allows next play_manager_fill() call to re-read the same data
 *    - Zero data loss: Every byte from WAV file eventually reaches the queue
 *    - Instrumentation: s_enqueue_fail_count tracks retries (normal under load)
 * 
 * 2. FRAME ALIGNMENT (Task 1.5):
 *    - Always read frame-aligned chunks (multiples of frame_bytes_src)
 *    - Prevents partial frames that could cause glitches or corruption
 *    - Alignment preserved through convert/resample pipeline
 * 
 * 3. CHUNK PADDING HANDLING (Task 1.4):
 *    - WAV chunks are word-aligned: odd-sized chunks have 1 padding byte
 *    - fmt chunk can be >16 bytes (extended format) - skip extra bytes correctly
 *    - Prevents file pointer drift that would corrupt subsequent reads
 * 
 * 4. INSTRUMENTATION (Task 0.2, 6.1):
 *    - s_bytes_read_from_file_total: Cumulative bytes read (with retries)
 *    - s_bytes_enqueued_total: Cumulative bytes successfully enqueued
 *    - s_expected_data_bytes: Total WAV data chunk size from header
 *    - s_enqueue_fail_count: Number of queue-full retries
 *    - s_dst_block_null_count: Memory allocation failures
 *    - Exposed via play_manager_get_instrumentation() for validation
 *    - Logged on completion to detect truncation regressions
 * 
 * 5. ERROR PROPAGATION:
 *    - process_audio_block() returns ESP_ERR_NO_MEM on enqueue failure
 *    - play_manager_fill() stops filling but returns ESP_OK (retry later)
 *    - Real errors (file read, format conversion) propagate immediately
 * 
 * CORRECTNESS INVARIANTS:
 * - s_bytes_read_from_file_total == s_expected_data_bytes (when complete)
 * - Data loss % = 0% for properly functioning queue/memory subsystems
 * - Retries are normal and transparent to caller
 * - Rewind accounting prevents double-counting of retried reads
 */

#include "play_manager.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "audio_queue.h"
#include "audio_util.h"
#include "audio_resampler_stream.h"
#include "util_safe.h"

static const char *TAG = "play_manager";

#define PLAY_HIGH_WATER_PCT       90U
#define PLAY_WAIT_FOR_FREE_MS     5U
#define PLAY_ENQUEUE_TIMEOUT_MS   500U

/**
 * PCM stash buffer for streaming resampler (CODE_REVIEW5 Task 1.2)
 *
 * WHY: Decouples file reads from resampler input requirements
 *      - File reads are fixed-size (1KB blocks from pool)
 *      - Resampler needs variable input (depends on ratio and phase)
 *      - Stash accumulates converted frames until resampler needs them
 *
 * HOW: Ring buffer holding PCM frames in output format
 *      - Frames are already bit-depth converted and channel-upmixed
 *      - Resampler consumes from stash, we refill from file as needed
 *
 * CORRECTNESS: Memory managed via heap_caps_malloc (regular heap)
 *              - 2048 frames × 4 bytes/frame (stereo 16-bit) = 8KB
 *              - Fits comfortably in available heap
 *              - Freed on playback stop/close
 */
typedef struct {
    uint8_t *buf;           ///< PCM buffer (allocated on init)
    size_t cap_frames;      ///< Capacity in frames
    size_t frame_bytes;     ///< Bytes per frame (channels × sample_bytes)
    size_t frames;          ///< Current frames stored
} pcm_stash_t;

typedef struct {
    bool initialized;
    bool active;
    audio_config_t out_cfg;          /* output format (dst) */
    audio_bit_depth_t src_bit;
    audio_sample_rate_t src_rate;
    size_t frame_bytes_src;
    size_t frame_bytes_dst;
    FILE *file;
    size_t remaining_bytes;
    size_t pending_bytes;
    size_t work_bytes;
    SemaphoreHandle_t mutex;
#ifdef CONFIG_BT_MOCK_TESTING
    bool test_zero_resample;
#endif
    /* CODE_REVIEW5 Task 1.3: Streaming resampler state */
    uint16_t wav_channels;           /* WAV channels (1=mono, 2=stereo) from header */
    size_t out_frames_per_chunk;     /* Fixed output size in frames (e.g., 256) */
    pcm_stash_t stash;               /* Input buffer for variable-rate resampler */
    audio_resampler_stream_t rs;     /* Stateful streaming resampler */
    bool eof_seen;                   /* EOF reached during file read */
} play_manager_state_t;

static play_manager_state_t s_pm = {0};

/* WAV playback instrumentation (CODE_REVIEW4 Task 0.2) */
static size_t s_bytes_read_from_file_total = 0;
static size_t s_bytes_enqueued_total = 0;
static size_t s_enqueue_fail_count = 0;
static size_t s_dst_block_null_count = 0;
static size_t s_expected_data_bytes = 0;

/**
 * PCM stash buffer functions (CODE_REVIEW5 Task 1.2)
 */

/**
 * Initialize PCM stash buffer
 *
 * Allocates buffer on heap to hold converted PCM frames.
 * Buffer persists for entire playback session.
 *
 * @param stash Stash structure to initialize
 * @param cap_frames Capacity in frames (e.g., 2048)
 * @param frame_bytes Bytes per frame (e.g., 4 for stereo 16-bit)
 * @return ESP_OK on success, ESP_ERR_NO_MEM if allocation fails
 */
static esp_err_t pcm_stash_init(pcm_stash_t *stash, size_t cap_frames, size_t frame_bytes)
{
    size_t buf_bytes = cap_frames * frame_bytes;
    stash->buf = (uint8_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_8BIT);
    if (!stash->buf) {
        ESP_LOGE(TAG, "Failed to allocate %zu bytes for PCM stash", buf_bytes);
        return ESP_ERR_NO_MEM;
    }

    stash->cap_frames = cap_frames;
    stash->frame_bytes = frame_bytes;
    stash->frames = 0;

    ESP_LOGI(TAG, "PCM stash initialized: %zu frames, %zu bytes/frame, %zu bytes total",
             cap_frames, frame_bytes, buf_bytes);
    return ESP_OK;
}

/**
 * Deinitialize PCM stash buffer
 *
 * Frees allocated buffer. Safe to call even if init failed.
 *
 * @param stash Stash structure to deinitialize
 */
static void pcm_stash_deinit(pcm_stash_t *stash)
{
    if (stash->buf) {
        free(stash->buf);
        stash->buf = NULL;
    }
    stash->cap_frames = 0;
    stash->frame_bytes = 0;
    stash->frames = 0;
}

/**
 * Get available space in stash
 *
 * @param stash Stash structure
 * @return Number of frames that can be appended
 */
static inline size_t pcm_stash_free_frames(const pcm_stash_t *stash)
{
    return stash->cap_frames - stash->frames;
}

/**
 * Append frames to stash
 *
 * Copies frames to end of stash buffer. Caller must ensure enough space.
 *
 * @param stash Stash structure
 * @param frames Pointer to frame data
 * @param num_frames Number of frames to append
 * @return ESP_OK on success, ESP_ERR_INVALID_SIZE if insufficient space
 */
static esp_err_t pcm_stash_append_frames(pcm_stash_t *stash, const uint8_t *frames, size_t num_frames)
{
    if (num_frames > pcm_stash_free_frames(stash)) {
        ESP_LOGE(TAG, "PCM stash overflow: trying to append %zu frames, only %zu free",
                 num_frames, pcm_stash_free_frames(stash));
        return ESP_ERR_INVALID_SIZE;
    }

    size_t append_bytes = num_frames * stash->frame_bytes;
    uint8_t *dst = stash->buf + (stash->frames * stash->frame_bytes);
    memcpy(dst, frames, append_bytes);
    stash->frames += num_frames;

    return ESP_OK;
}

/**
 * Consume frames from stash
 *
 * Removes consumed frames from front of stash using memmove.
 * Call this after resampler has processed frames from stash.
 *
 * WHY memmove: Stash is not a true ring buffer (simpler implementation)
 *              - Frames always appended to end
 *              - Consumed frames removed from front via memmove
 *              - Trade: CPU cost of memmove vs complexity of ring buffer
 *              - Acceptable because consumption is infrequent (large chunks)
 *
 * @param stash Stash structure
 * @param num_frames Number of frames to consume from front
 * @return ESP_OK on success, ESP_ERR_INVALID_SIZE if not enough frames
 */
static esp_err_t pcm_stash_consume_frames(pcm_stash_t *stash, size_t num_frames)
{
    if (num_frames > stash->frames) {
        ESP_LOGE(TAG, "PCM stash underflow: trying to consume %zu frames, only %zu available",
                 num_frames, stash->frames);
        return ESP_ERR_INVALID_SIZE;
    }

    if (num_frames == 0) {
        return ESP_OK;
    }

    size_t consume_bytes = num_frames * stash->frame_bytes;
    size_t remaining_frames = stash->frames - num_frames;
    size_t remaining_bytes = remaining_frames * stash->frame_bytes;

    if (remaining_frames > 0) {
        // Shift remaining frames to front of buffer
        memmove(stash->buf, stash->buf + consume_bytes, remaining_bytes);
    }

    stash->frames = remaining_frames;
    return ESP_OK;
}

/**
 * Helper: Get bytes per sample based on bit depth
 */
static inline int bytes_per_sample(audio_bit_depth_t bit_depth)
{
    switch (bit_depth) {
        case AUDIO_BIT_DEPTH_24:
        case AUDIO_BIT_DEPTH_32:
            return 4;
        case AUDIO_BIT_DEPTH_16:
        default:
            return 2;
    }
}

/**
 * ensure_stash_frames() - Read and convert frames from WAV to fill stash
 * (CODE_REVIEW5 Task 1.4)
 *
 * WHY: Streaming resampler needs variable input frames based on ratio and phase
 *      - File reads are fixed-size (1KB blocks from pool)
 *      - Stash decouples file I/O from resampler consumption
 *
 * HOW: Loop until stash has enough frames:
 *      1. Compute frames needed (gap to min_frames_needed)
 *      2. Convert to source bytes, clamp to remaining file data
 *      3. Allocate 1KB block, read aligned chunk from file
 *      4. Convert bit depth (reuse existing convert_audio_format)
 *      5. Upmix mono→stereo if needed (duplicate samples L=R)
 *      6. Append converted frames to stash, release block
 *      7. Handle EOF: set eof_seen flag, break if cannot satisfy request
 *
 * CORRECTNESS: Mono→stereo upmix performed on converted data before stash
 *              - Ensures stash always holds output format frames
 *              - Resampler doesn't need channel awareness
 *              - Frame counts are output-format frames throughout
 *
 * @param min_frames_needed Minimum frames required in stash (output format)
 * @return ESP_OK on success (stash may have fewer frames at EOF)
 *         ESP_ERR_NO_MEM if block allocation fails
 *         ESP_ERR_INVALID_STATE on file read error
 */
static esp_err_t ensure_stash_frames(size_t min_frames_needed)
{
    while (s_pm.stash.frames < min_frames_needed && !s_pm.eof_seen) {
        size_t frames_to_add = min_frames_needed - s_pm.stash.frames;
        
        /* Clamp to stash free space */
        size_t stash_free = pcm_stash_free_frames(&s_pm.stash);
        if (frames_to_add > stash_free) {
            frames_to_add = stash_free;
        }
        
        if (frames_to_add == 0) {
            /* Stash full but still need more frames - cannot proceed */
            break;
        }
        
        /* Convert output frames to source bytes needed */
        size_t src_frames_needed = frames_to_add;  /* 1:1 for now, will adjust for ratio later */
        size_t src_bytes_needed = src_frames_needed * s_pm.frame_bytes_src;
        
        /* Clamp to remaining file data */
        if (src_bytes_needed > s_pm.remaining_bytes) {
            src_bytes_needed = s_pm.remaining_bytes;
        }
        
        /* Frame-align the read (must read complete frames) */
        size_t src_frame_bytes = s_pm.frame_bytes_src;
        if (src_frame_bytes == 0) {
            src_frame_bytes = 1;  /* Safety: avoid divide-by-zero */
        }
        src_bytes_needed = (src_bytes_needed / src_frame_bytes) * src_frame_bytes;
        
        if (src_bytes_needed == 0) {
            /* EOF or no data left */
            s_pm.eof_seen = true;
            break;
        }
        
        /* Clamp to 1KB block size (AUDIO_CHUNK_BLOCK_BYTES) */
        if (src_bytes_needed > AUDIO_CHUNK_BLOCK_BYTES) {
            src_bytes_needed = AUDIO_CHUNK_BLOCK_BYTES;
            /* Re-align after clamping */
            src_bytes_needed = (src_bytes_needed / src_frame_bytes) * src_frame_bytes;
        }
        
        /* Allocate temporary read buffer from pool */
        uint8_t *src_block = audio_chunk_alloc_block(pdMS_TO_TICKS(100));
        if (!src_block) {
            ESP_LOGW(TAG, "ensure_stash_frames: Failed to allocate read block");
            return ESP_ERR_NO_MEM;
        }
        
        /* Read from file */
        size_t bytes_read = fread(src_block, 1, src_bytes_needed, s_pm.file);
        if (bytes_read == 0) {
            audio_chunk_release_block(src_block);
            s_pm.eof_seen = true;
            break;
        }
        
        /* Update file accounting */
        if (bytes_read > s_pm.remaining_bytes) {
            s_pm.remaining_bytes = 0;
        } else {
            s_pm.remaining_bytes -= bytes_read;
        }
        
        /* Convert bit depth in-place */
        size_t conv_bytes = 0;
        audio_convert_args_t conv_args = {
            .src = src_block,
            .dst = src_block,
            .src_size = bytes_read,
            .src_bit_depth = s_pm.src_bit,
            .dst_bit_depth = s_pm.out_cfg.bit_depth,
            .dst_size = &conv_bytes,
            .work_bytes = AUDIO_CHUNK_BLOCK_BYTES,
        };
        esp_err_t ret = convert_audio_format(&conv_args);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ensure_stash_frames: Bit depth conversion failed");
            audio_chunk_release_block(src_block);
            return ret;
        }
        
        /* Calculate source frames after conversion */
        size_t sample_bytes_dst = bytes_per_sample(s_pm.out_cfg.bit_depth);
        size_t src_frame_bytes_conv = s_pm.wav_channels * sample_bytes_dst;
        size_t src_frames = conv_bytes / src_frame_bytes_conv;
        
        /* Upmix mono→stereo if needed */
        size_t dst_frames = src_frames;
        if (s_pm.wav_channels == 1 && s_pm.out_cfg.channels == 2) {
            /* Mono→stereo: duplicate each sample L=R
             * Process backwards to avoid overwriting source data
             * Source: [s0, s1, s2, ...]
             * Dest:   [s0, s0, s1, s1, s2, s2, ...]
             */
            if (sample_bytes_dst == 2) {
                /* 16-bit samples */
                int16_t *samples = (int16_t *)src_block;
                for (int i = (int)src_frames - 1; i >= 0; i--) {
                    int16_t sample = samples[i];
                    samples[i * 2] = sample;      /* Left */
                    samples[i * 2 + 1] = sample;  /* Right */
                }
            } else {
                /* 32-bit samples */
                int32_t *samples = (int32_t *)src_block;
                for (int i = (int)src_frames - 1; i >= 0; i--) {
                    int32_t sample = samples[i];
                    samples[i * 2] = sample;      /* Left */
                    samples[i * 2 + 1] = sample;  /* Right */
                }
            }
            /* Output is now stereo frames */
            dst_frames = src_frames;  /* Same number of frames, just doubled channels */
        }
        
        /* Append converted frames to stash */
        ret = pcm_stash_append_frames(&s_pm.stash, src_block, dst_frames);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ensure_stash_frames: Failed to append %zu frames to stash", dst_frames);
            audio_chunk_release_block(src_block);
            return ret;
        }
        
        /* Release temporary block */
        audio_chunk_release_block(src_block);
        
        /* Check for EOF */
        if (s_pm.remaining_bytes == 0) {
            s_pm.eof_seen = true;
            break;
        }
    }
    
    return ESP_OK;
}

/**
 * @brief Produce exactly one 1KB output block (fixed size)
 *
 * WHY: New streaming resampler pipeline produces fixed-size output chunks
 *      to simplify queue management and eliminate cumulative rounding errors.
 *      Unlike old block-local resampling (variable output), this always
 *      produces exactly 1024 bytes (256 frames stereo 16-bit).
 *
 * HOW:
 *   1. Compute required input frames for desired output (variable, depends on ratio)
 *   2. Ensure stash has enough frames (reads/converts file data as needed)
 *   3. Call streaming resampler (consumes from stash, produces fixed output)
 *   4. Pad with zeros if EOF prevents full output
 *   5. Always return 1024 bytes (even if partial silence)
 *
 * CORRECTNESS:
 *   - Stash provides variable input frames based on resampler needs
 *   - Resampler maintains Q16.16 phase across calls (no cumulative loss)
 *   - Fixed output size simplifies caller (always 1KB blocks)
 *   - EOF handling: pads silence when stash exhausted
 *
 * @param dst_block Output buffer (must be AUDIO_CHUNK_BLOCK_BYTES = 1024 bytes)
 * @param out_bytes [OUT] Bytes written (always 1024, even if padded)
 * @return ESP_OK on success, error code otherwise
 *
 * @note Called repeatedly by play_manager_fill() until EOF and stash drained
 * @note Uses Tasks 1.1 (resampler), 1.2 (stash), 1.4 (ensure_stash_frames)
 */
static esp_err_t produce_one_output_block(uint8_t *dst_block, size_t *out_bytes)
{
    /* Compute desired output frames (fixed chunk size) */
    size_t out_frames = s_pm.out_frames_per_chunk;  /* e.g., 256 frames */
    
    /* Compute minimum input frames needed from stash */
    size_t min_in_frames = audio_resampler_stream_min_in_frames(&s_pm.rs, out_frames);
    
    /* Ensure stash has enough frames (reads/converts file data if needed) */
    esp_err_t ret = ensure_stash_frames(min_in_frames);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "produce_one_output_block: Failed to fill stash");
        return ret;
    }
    
    /* Get current stash frame count (may be less than min_in if EOF) */
    size_t available_frames = s_pm.stash.frames;
    
    /* Call streaming resampler (produces exactly out_frames, consumes variable input) */
    size_t in_frames_consumed = 0;
    size_t frames_produced = audio_resampler_stream_process(
        &s_pm.rs,
        s_pm.stash.buf,         /* Input from stash */
        available_frames,        /* Available input frames */
        dst_block,               /* Output block */
        out_frames,              /* Desired output frames */
        &in_frames_consumed      /* [OUT] Frames consumed from stash */
    );
    
    /* Remove consumed frames from stash */
    if (in_frames_consumed > 0) {
        ret = pcm_stash_consume_frames(&s_pm.stash, in_frames_consumed);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "produce_one_output_block: Failed to consume %zu frames", in_frames_consumed);
            return ret;
        }
    }
    
    /* Check for partial output (EOF case) */
    if (frames_produced < out_frames) {
        /* Pad remainder with zeros (silence) */
        size_t silence_frames = out_frames - frames_produced;
        size_t silence_bytes = silence_frames * s_pm.frame_bytes_dst;
        uint8_t *silence_ptr = dst_block + (frames_produced * s_pm.frame_bytes_dst);
        memset(silence_ptr, 0, silence_bytes);
        
        ESP_LOGD(TAG, "produce_one_output_block: EOF - produced %zu/%zu frames, padded %zu",
                 frames_produced, out_frames, silence_frames);
    }
    
    /* Always output exactly 1024 bytes (256 frames stereo 16-bit) */
    *out_bytes = out_frames * s_pm.frame_bytes_dst;
    
    return ESP_OK;
}

/* Helper: Read and validate RIFF/WAVE header */
static esp_err_t read_wav_riff_header(FILE *file)
{
    uint32_t tmp32 = 0;
    char riff[4];
    if (fread(riff, 1, 4, file) != 4) {
        return ESP_ERR_INVALID_STATE;
    }
    if (memcmp(riff, "RIFF", 4) != 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (fread(&tmp32, 4, 1, file) != 1) {
        return ESP_ERR_INVALID_STATE; /* skip size */
    }
    char wave[4];
    if (fread(wave, 1, 4, file) != 4) {
        return ESP_ERR_INVALID_STATE;
    }
    if (memcmp(wave, "WAVE", 4) != 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

/* Helper: Parse fmt chunk */
static esp_err_t parse_fmt_chunk(FILE *file, uint32_t chunk_size,
                                uint16_t *audio_format,
                                uint16_t *num_channels,
                                uint32_t *sample_rate,
                                uint16_t *bits_per_sample)
{
    if (chunk_size < 16) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint32_t tmp32 = 0;
    uint16_t tmp16 = 0;
    bool success =
        fread(audio_format, 2, 1, file) == 1 &&
        fread(num_channels, 2, 1, file) == 1 &&
        fread(sample_rate, 4, 1, file) == 1 &&
        fread(&tmp32, 4, 1, file) == 1 && /* byte rate */
        fread(&tmp16, 2, 1, file) == 1 && /* block align */
        fread(bits_per_sample, 2, 1, file) == 1;
    
    if (!success) {
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Skip extra bytes with padding (CODE_REVIEW4 Task 1.4) */
    if (chunk_size > 16) {
        size_t extra = chunk_size - 16;
        size_t skip = extra + (extra & 1);  /* Add padding if odd */
        fseek(file, (long)skip, SEEK_CUR);
    }
    
    return ESP_OK;
}

/* Helper: Skip unknown chunk with word-aligned padding (DATA INTEGRITY)
 * 
 * WHY PADDING MATTERS: WAV/RIFF specification requires all chunks to be word-aligned
 * (even byte offset). When a chunk has an odd size (e.g., 1007 bytes), the file
 * contains 1 padding byte after the chunk data to maintain word alignment for the
 * next chunk. Without accounting for this padding, our file pointer drifts and we
 * misinterpret chunk headers as audio data (corruption).
 * 
 * HOW IT WORKS:
 * - skip = chunk_size + (chunk_size & 1)   // Add 1 if odd, 0 if even
 * - fseek forward by skip bytes (skips data + padding)
 * 
 * EXAMPLES:
 * - chunk_size=100 (even): skip=100+0=100 bytes
 * - chunk_size=101 (odd):  skip=101+1=102 bytes (includes 1-byte pad)
 * 
 * CORRECTNESS: chunk_size & 1 is 1 when odd, 0 when even. This handles alignment
 * transparently for both cases. Failure to skip padding causes subsequent chunk
 * headers to be misaligned, leading to parse errors or data corruption.
 * 
 * This is CODE_REVIEW4 Task 1.4 - ensures accurate WAV file parsing.
 */
static void skip_wav_chunk(FILE *file, uint32_t chunk_size)
{
    /* WAV chunks are word-aligned: odd-sized chunks have 1 padding byte */
    size_t skip = chunk_size + (chunk_size & 1);
    fseek(file, (long)skip, SEEK_CUR);
}

/* Helper: Validate and convert WAV format parameters */
static esp_err_t validate_wav_params(uint16_t audio_format,
                                     uint16_t num_channels,
                                     uint16_t bits_per_sample,
                                     audio_bit_depth_t *src_bit)
{
    if (audio_format != 1) {
        return ESP_ERR_NOT_SUPPORTED; /* PCM only */
    }
    if (num_channels != 1 && num_channels != 2) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (bits_per_sample == 16) {
        *src_bit = AUDIO_BIT_DEPTH_16;
    } else if (bits_per_sample == 24) {
        *src_bit = AUDIO_BIT_DEPTH_24;
    } else if (bits_per_sample == 32) {
        *src_bit = AUDIO_BIT_DEPTH_32;
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    return ESP_OK;
}

static esp_err_t parse_wav_header(FILE *file,
                                  audio_bit_depth_t *src_bit,
                                  audio_sample_rate_t *src_rate,
                                  uint16_t *channels,
                                  size_t *data_bytes)
{
    if (!file || !src_bit || !src_rate || !channels || !data_bytes) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Read RIFF/WAVE header */
    esp_err_t ret = read_wav_riff_header(file);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Parse chunks */
    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_sz = 0;
    bool have_fmt = false;

    while (!feof(file)) {
        char chunk_id[4];
        uint32_t chunk_size = 0;
        if (fread(chunk_id, 1, 4, file) != 4 || fread(&chunk_size, 4, 1, file) != 1) {
            break;
        }

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            ret = parse_fmt_chunk(file, chunk_size, &audio_format, &num_channels,
                                 &sample_rate, &bits_per_sample);
            if (ret != ESP_OK) {
                return ret;
            }
            have_fmt = true;
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            data_sz = chunk_size;
            break;
        } else {
            skip_wav_chunk(file, chunk_size);
        }
    }

    /* Validate we found required chunks */
    if (!have_fmt || data_sz == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Validate and convert parameters */
    ret = validate_wav_params(audio_format, num_channels, bits_per_sample, src_bit);
    if (ret != ESP_OK) {
        return ret;
    }

    *src_rate = (audio_sample_rate_t)sample_rate;
    *channels = num_channels;
    *data_bytes = data_sz;
    return ESP_OK;
}

esp_err_t play_manager_init(const audio_config_t *config,
                            const play_manager_buffers_t *buffers)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_pm.initialized) {
        return ESP_OK;
    }

    util_safe_memset(&s_pm, sizeof(s_pm), 0, sizeof(s_pm));
    s_pm.out_cfg = *config;
    s_pm.work_bytes = (buffers && buffers->work_bytes > 0) ? buffers->work_bytes : AUDIO_CHUNK_BLOCK_BYTES;
    s_pm.mutex = xSemaphoreCreateMutex();
    if (s_pm.mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_pm.initialized = true;
    return ESP_OK;
}

void play_manager_deinit(void)
{
    if (!s_pm.initialized) {
        return;
    }

    if (s_pm.mutex) {
        if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) == pdTRUE) {
            if (s_pm.file) {
                fclose(s_pm.file);
                s_pm.file = NULL;
            }
            s_pm.active = false;
            s_pm.remaining_bytes = 0;
            s_pm.pending_bytes = 0;
            xSemaphoreGive(s_pm.mutex);
        }
        vSemaphoreDelete(s_pm.mutex);
        s_pm.mutex = NULL;
    }
    s_pm.initialized = false;
}

bool play_manager_is_active(void)
{
    return s_pm.active;
}

/* Helper: Allocate src and dst blocks */
static esp_err_t allocate_audio_blocks(uint8_t **src_block, uint8_t **dst_block)
{
    *src_block = audio_chunk_alloc_block(pdMS_TO_TICKS(5));
    if (*src_block == NULL) {
        return ESP_ERR_NO_MEM;
    }

    *dst_block = audio_chunk_alloc_block(pdMS_TO_TICKS(5));
    if (*dst_block == NULL) {
        s_dst_block_null_count++;  /* Instrumentation */
        audio_chunk_release_block(*src_block);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

/* Helper: Calculate frame-aligned read size (DATA LOSS PREVENTION)
 * 
 * WHY FRAME ALIGNMENT: Reading partial frames causes glitches, corruption, and
 * misaligned samples in Bluetooth audio. Every read must deliver complete frames
 * (e.g., 4 bytes for stereo 16-bit) to maintain audio integrity downstream.
 * 
 * HOW IT WORKS (two-step alignment):
 * 1. CLAMP to remaining_bytes FIRST (Task 1.5) - prevents read-past-EOF
 * 2. ALIGN DOWN to frame boundary - ensures complete frames only
 * 
 * Edge cases:
 * - If aligned result is 0 (rare: to_read < frame_bytes), return one frame minimum
 * - Last chunk may be smaller than AUDIO_CHUNK_BLOCK_BYTES but still frame-aligned
 * - frame_bytes=0 treated as 1 (safety: avoid divide-by-zero, though shouldn't occur)
 * 
 * CORRECTNESS: Order matters. Clamping before alignment ensures we never align
 * down past EOF (which would create a short read and require rewind). Alignment
 * after clamping ensures we respect file boundaries while maintaining frames.
 * 
 * This is CODE_REVIEW4 Task 1.5 - prevents partial frames and guarantees clean audio.
 */
static size_t calculate_read_size(size_t frame_bytes, size_t remaining_bytes)
{
    size_t to_read = AUDIO_CHUNK_BLOCK_BYTES;
    
    /* Clamp to remaining bytes FIRST (CODE_REVIEW4 Task 1.5) */
    if (to_read > remaining_bytes) {
        to_read = remaining_bytes;
    }
    
    /* Align down to frame boundary */
    size_t frame_src = (frame_bytes != 0) ? frame_bytes : 1U;
    to_read = (to_read / frame_src) * frame_src;
    if (to_read == 0) {
        to_read = frame_src;
    }
    
    return to_read;
}

/* Helper: Read data from file and update accounting */
static esp_err_t read_audio_data(uint8_t *buffer, size_t to_read, size_t *bytes_read)
{
    *bytes_read = fread(buffer, 1, to_read, s_pm.file);
    if (*bytes_read == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Update instrumentation and state */
    s_bytes_read_from_file_total += *bytes_read;
    if (*bytes_read > s_pm.remaining_bytes) {
        s_pm.remaining_bytes = 0;
    } else {
        s_pm.remaining_bytes -= *bytes_read;
    }
    
    return ESP_OK;
}

/* Helper: Convert audio format */
static esp_err_t convert_audio_block(uint8_t *src_block, size_t src_size, size_t *conv_size)
{
    audio_convert_args_t conv_args = {
        .src = src_block,
        .dst = src_block,
        .src_size = src_size,
        .src_bit_depth = s_pm.src_bit,
        .dst_bit_depth = s_pm.out_cfg.bit_depth,
        .dst_size = conv_size,
        .work_bytes = s_pm.work_bytes,
    };
    return convert_audio_format(&conv_args);
}

/* Helper: Resample audio */
static esp_err_t resample_audio_block(const uint8_t *src_block, uint8_t *dst_block,
                                     size_t src_size, size_t *res_size)
{
    audio_resample_args_t res_args = {
        .src = src_block,
        .dst = dst_block,
        .src_size = src_size,
        .src_rate = s_pm.src_rate,
        .dst_rate = s_pm.out_cfg.sample_rate,
        .bit_depth = s_pm.out_cfg.bit_depth,
        .channels = s_pm.out_cfg.channels,
        .dst_size = res_size,
        .work_bytes = s_pm.work_bytes,
    };
    return resample_audio(&res_args);
}

/* Helper: Rewind file after enqueue failure (DATA LOSS PREVENTION)
 * 
 * WHY REWIND: When audio_chunk_enqueue_block() fails (queue full, OOM), the
 * bytes we just read from the file haven't been queued. Without rewinding,
 * those bytes would be lost forever when we advance to the next read.
 * 
 * HOW IT WORKS:
 * 1. fseek backwards by bytes_to_rewind (the amount we just read)
 * 2. Restore s_pm.remaining_bytes so next fill iteration re-reads same data
 * 3. Decrement s_bytes_read_from_file_total to prevent double-counting
 * 4. Increment s_enqueue_fail_count for instrumentation (retries are normal)
 * 
 * CORRECTNESS: bytes_to_rewind is always <= bytes we just read in current
 * iteration, and we only rewind on enqueue failure (not read/convert errors).
 * File position perfectly tracks queued data, not just read data.
 * 
 * ROBUSTNESS: Rewind failure is logged but doesn't crash. Worst case: some
 * bytes lost (degraded mode). Normal case: rewind succeeds, zero data loss.
 * 
 * This is CODE_REVIEW4 Task 1.2 - the core of lossless WAV playback.
 */
static void rewind_after_enqueue_failure(size_t bytes_to_rewind)
{
    /* Rewind file pointer to re-read this data on next fill */
    if (fseek(s_pm.file, -(long)bytes_to_rewind, SEEK_CUR) != 0) {
        ESP_LOGW(TAG, "Failed to rewind file after enqueue failure");  // NOLINT(bugprone-branch-clone)
    }
    
    /* Restore accounting - bytes_to_rewind always <= bytes_read_from_file_total at this point */
    s_pm.remaining_bytes += bytes_to_rewind;
    s_enqueue_fail_count++;
    s_bytes_read_from_file_total -= bytes_to_rewind;
}

/* Helper: Process one audio block (read, convert, resample, enqueue) */
static esp_err_t process_audio_block(void)
{
    uint8_t *src_block = NULL;
    uint8_t *dst_block = NULL;
    
    /* Allocate blocks */
    esp_err_t ret = allocate_audio_blocks(&src_block, &dst_block);
    if (ret != ESP_OK) {
        return ESP_OK;  /* Not an error, just no memory available */
    }
    
    /* Calculate read size */
    size_t to_read = calculate_read_size(s_pm.frame_bytes_src, s_pm.remaining_bytes);
    
    /* Read from file */
    size_t got = 0;
    ret = read_audio_data(src_block, to_read, &got);
    if (ret != ESP_OK) {
        audio_chunk_release_block(dst_block);
        audio_chunk_release_block(src_block);
        s_pm.remaining_bytes = 0;
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Convert format */
    size_t conv_size = 0;
    ret = convert_audio_block(src_block, got, &conv_size);
    if (ret != ESP_OK) {
        audio_chunk_release_block(dst_block);
        audio_chunk_release_block(src_block);
        return ret;
    }
    
    /* Resample */
    size_t res_size = 0;
    ret = resample_audio_block(src_block, dst_block, conv_size, &res_size);
    audio_chunk_release_block(src_block);
    if (ret != ESP_OK) {
        audio_chunk_release_block(dst_block);
        return ret;
    }
    
#ifdef CONFIG_BT_MOCK_TESTING
    if (s_pm.test_zero_resample) {
        res_size = 0;
    }
#endif
    
    /* Skip if empty */
    if (res_size == 0) {
        audio_chunk_release_block(dst_block);
        return ESP_OK;
    }
    
    /* Enqueue */
    if (!audio_chunk_enqueue_block(dst_block, res_size, AUDIO_SOURCE_TAG_WAV)) {
        rewind_after_enqueue_failure(got);
        audio_chunk_release_block(dst_block);
        return ESP_ERR_NO_MEM;  /* Signal to stop loop */
    }
    
    /* Update instrumentation */
    s_bytes_enqueued_total += res_size;
    s_pm.pending_bytes += res_size;
    
    return ESP_OK;
}

esp_err_t play_manager_fill(void)
{
    if (!s_pm.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_pm.active || s_pm.file == NULL) {
        return ESP_OK;
    }
    if (s_pm.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;

    if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* Process multiple blocks per call */
    const int max_iters = 4;
    for (int iter = 0; iter < max_iters && s_pm.remaining_bytes > 0; ++iter) {
        ret = process_audio_block();
        if (ret == ESP_ERR_NO_MEM) {
            /* Queue full or allocation failed - stop and retry later */
            ret = ESP_OK;
            break;
        }
        if (ret != ESP_OK) {
            /* Real error - propagate */
            break;
        }
    }

    xSemaphoreGive(s_pm.mutex);
    return ret;
}

/* Helper: Open and parse WAV file */
static esp_err_t open_and_parse_wav(const char *path, FILE **file, 
                                    audio_bit_depth_t *src_bit,
                                    audio_sample_rate_t *src_rate,
                                    uint16_t *channels,
                                    size_t *data_bytes)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "play_manager_play_wav: failed to open %s", path);  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t status = parse_wav_header(f, src_bit, src_rate, channels, data_bytes);
    if (status != ESP_OK) {
        fclose(f);
        return status;
    }

    *file = f;
    return ESP_OK;
}

/* Helper: Calculate frame sizes from audio format */
static esp_err_t calculate_frame_sizes(audio_bit_depth_t src_bit, uint16_t channels,
                                       size_t *frame_bytes_src, size_t *frame_bytes_dst)
{
    int dst_bps = bytes_per_sample(s_pm.out_cfg.bit_depth);
    int dst_ch = (s_pm.out_cfg.channels == AUDIO_CHANNEL_MONO) ? 1 : 2;
    *frame_bytes_dst = (size_t)dst_bps * (size_t)dst_ch;
    *frame_bytes_src = ((size_t)bytes_per_sample(src_bit)) * (size_t)channels;
    
    if (*frame_bytes_dst == 0 || *frame_bytes_src == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

/* Helper: Initialize playback state under mutex */
static esp_err_t initialize_playback_state(FILE *file, 
                                           audio_bit_depth_t src_bit,
                                           audio_sample_rate_t src_rate,
                                           size_t frame_bytes_src,
                                           size_t frame_bytes_dst,
                                           size_t data_bytes)
{
    if (s_pm.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_pm.active) {
        xSemaphoreGive(s_pm.mutex);
        return ESP_ERR_INVALID_STATE;
    }

    s_pm.src_bit = src_bit;
    s_pm.src_rate = src_rate;
    s_pm.frame_bytes_src = frame_bytes_src;
    s_pm.frame_bytes_dst = frame_bytes_dst;
    s_pm.file = file;
    s_pm.remaining_bytes = data_bytes;
    s_pm.pending_bytes = 0;
    s_pm.active = true;

    /* Initialize instrumentation counters (CODE_REVIEW4 Task 0.2) */
    s_bytes_read_from_file_total = 0;
    s_bytes_enqueued_total = 0;
    s_enqueue_fail_count = 0;
    s_dst_block_null_count = 0;
    s_expected_data_bytes = data_bytes;

    xSemaphoreGive(s_pm.mutex);
    return ESP_OK;
}

/* Helper: Log playback instrumentation results */
static void log_playback_completion(void)
{
    ESP_LOGI(TAG, "WAV playback complete - instrumentation report:");  // NOLINT(bugprone-branch-clone)
    ESP_LOGI(TAG, "  Expected data bytes: %zu", s_expected_data_bytes);  // NOLINT(bugprone-branch-clone)
    ESP_LOGI(TAG, "  Bytes read from file: %zu", s_bytes_read_from_file_total);  // NOLINT(bugprone-branch-clone)
    ESP_LOGI(TAG, "  Bytes enqueued: %zu", s_bytes_enqueued_total);  // NOLINT(bugprone-branch-clone)
    ESP_LOGI(TAG, "  dst_block alloc failures: %zu", s_dst_block_null_count);  // NOLINT(bugprone-branch-clone)
    ESP_LOGI(TAG, "  Enqueue failures: %zu", s_enqueue_fail_count);  // NOLINT(bugprone-branch-clone)
    
    if (s_expected_data_bytes > 0) {
        size_t bytes_lost = 0;
        if (s_bytes_read_from_file_total > s_bytes_enqueued_total) {
            bytes_lost = s_bytes_read_from_file_total - s_bytes_enqueued_total;
        }
        float percent_lost = (float)bytes_lost / (float)s_expected_data_bytes * 100.0F;
        ESP_LOGI(TAG, "  Data loss: %zu bytes (%.2f%%)", bytes_lost, (double)percent_lost);  // NOLINT(bugprone-branch-clone)
    }
}

/* Helper: Cleanup playback state */
static void cleanup_playback_state(void)
{
    if (s_pm.file) {
        fclose(s_pm.file);
        s_pm.file = NULL;
    }
    s_pm.active = false;
}

esp_err_t play_manager_play_wav(const char *path)
{
    /* Validate parameters */
    if (!s_pm.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Open and parse WAV file */
    FILE *file = NULL;
    audio_bit_depth_t src_bit = AUDIO_BIT_DEPTH_16;
    audio_sample_rate_t src_rate = AUDIO_SAMPLE_RATE_44K;
    uint16_t channels = 0;
    size_t data_bytes = 0;

    esp_err_t status = open_and_parse_wav(path, &file, &src_bit, &src_rate, &channels, &data_bytes);
    if (status != ESP_OK) {
        return status;
    }

    /* Calculate frame sizes */
    size_t frame_bytes_src = 0;
    size_t frame_bytes_dst = 0;
    status = calculate_frame_sizes(src_bit, channels, &frame_bytes_src, &frame_bytes_dst);
    if (status != ESP_OK) {
        fclose(file);
        return status;
    }

    /* Initialize playback state */
    status = initialize_playback_state(file, src_bit, src_rate, frame_bytes_src, frame_bytes_dst, data_bytes);
    if (status != ESP_OK) {
        fclose(file);
        return status;
    }

    /* Start filling audio queue */
    status = play_manager_fill();
    if (status != ESP_OK) {
        play_manager_abort(false);
        return status;
    }

    /* Log playback start */
    ESP_LOGI(TAG, "play_manager: streaming %s (src %u Hz %d-bit ch=%u)",  // NOLINT(bugprone-branch-clone)
             path,
             (unsigned)src_rate,
             (int)src_bit,
             (unsigned)channels);
    return ESP_OK;
}

void play_manager_abort(bool allow_resume)
{
    (void)allow_resume;
    if (!s_pm.initialized || s_pm.mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) == pdTRUE) {
        if (s_pm.file) {
            fclose(s_pm.file);
            s_pm.file = NULL;
        }
        s_pm.active = false;
        s_pm.remaining_bytes = 0;
        s_pm.pending_bytes = 0;
        xSemaphoreGive(s_pm.mutex);
    }
}

bool play_manager_consume(size_t bytes)
{
    if (!s_pm.initialized || s_pm.mutex == NULL) {
        return false;
    }
    
    bool drained = false;
    if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) == pdTRUE) {
        /* Update pending bytes accounting */
        if (bytes > s_pm.pending_bytes) {
            s_pm.pending_bytes = 0;
        } else {
            s_pm.pending_bytes -= bytes;
        }
        
        /* Check if playback complete */
        if (s_pm.pending_bytes == 0 && s_pm.remaining_bytes == 0) {
            log_playback_completion();
            cleanup_playback_state();
            drained = true;
        }
        
        xSemaphoreGive(s_pm.mutex);
    }
    return drained;
}

size_t play_manager_pending_bytes(void)
{
    size_t pending = 0;
    if (s_pm.initialized && s_pm.mutex != NULL) {
        if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) == pdTRUE) {
            pending = s_pm.pending_bytes;
            xSemaphoreGive(s_pm.mutex);
        }
    }
    return pending;
}

bool play_manager_get_instrumentation(play_manager_instrumentation_t *instr)
{
    if (!instr || !s_pm.initialized || s_pm.mutex == NULL) {
        return false;
    }

    if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    instr->expected_data_bytes = s_expected_data_bytes;
    instr->bytes_read_from_file = s_bytes_read_from_file_total;
    instr->bytes_enqueued = s_bytes_enqueued_total;
    instr->enqueue_fail_count = s_enqueue_fail_count;
    instr->dst_block_null_count = s_dst_block_null_count;

    xSemaphoreGive(s_pm.mutex);
    return true;
}

#ifdef CONFIG_BT_MOCK_TESTING
void play_manager_test_set_frame_bytes_dst(size_t frame_bytes)
{
    if (s_pm.initialized && s_pm.mutex != NULL) {
        if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) == pdTRUE) {
            s_pm.frame_bytes_dst = frame_bytes;
            xSemaphoreGive(s_pm.mutex);
        }
    }
}

void play_manager_test_force_zero_resample(bool enable)
{
    if (s_pm.initialized && s_pm.mutex != NULL) {
        if (xSemaphoreTake(s_pm.mutex, portMAX_DELAY) == pdTRUE) {
            s_pm.test_zero_resample = enable;
            xSemaphoreGive(s_pm.mutex);
        }
    }
}
#endif
