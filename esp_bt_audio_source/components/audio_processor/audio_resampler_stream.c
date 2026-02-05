/**
 * @file audio_resampler_stream.c
 * @brief Stateful streaming resampler implementation
 *
 * Implements linear interpolation with Q16.16 fixed-point phase accumulation
 * to eliminate cumulative rounding errors from block-local resampling.
 *
 * Key insight: Phase must carry across blocks to maintain sample-accurate
 * output duration. Block-local floor() loses fractional frames each iteration.
 */

#include "audio_resampler_stream.h"
#include <string.h>
#include <esp_log.h>
#include "util_safe.h"

static const char *TAG = "audio_resampler_stream";

/**
 * Q16.16 fixed-point helpers
 *
 * Format: 16 bits integer, 16 bits fractional
 * Range: 0.0 to 65535.999984741
 * Precision: 1/65536 ≈ 0.0000152587890625
 */
#define Q16_FRAC_BITS 16
#define Q16_ONE (1U << Q16_FRAC_BITS)  // 0x10000 = 1.0
#define Q16_INT(x) ((x) >> Q16_FRAC_BITS)
#define Q16_FRAC(x) ((x) & (Q16_ONE - 1))

/**
 * Linear interpolation helper (16-bit samples)
 *
 * y = (1 - frac) * s0 + frac * s1
 *   = s0 + frac * (s1 - s0)
 *
 * Using Q16.16 arithmetic:
 * - frac is already in Q16 format (0x0000 to 0xFFFF)
 * - diff = s1 - s0 (signed, -32768 to 32767)
 * - product = frac * diff (32-bit intermediate)
 * - result = s0 + (product >> 16)
 *
 * WHY this works: frac/Q16_ONE gives the weight, but we keep it in Q16
 * to preserve precision through the multiplication.
 */
static inline int16_t lerp_i16(int16_t s0, int16_t s1, uint32_t frac_q16)
{
    int32_t diff = (int32_t)s1 - (int32_t)s0;
    int32_t product = diff * (int32_t)frac_q16;
    return s0 + (int16_t)(product >> Q16_FRAC_BITS);
}

/**
 * Linear interpolation helper (32-bit samples)
 *
 * Same algorithm as lerp_i16 but with 32-bit samples.
 * Uses 64-bit intermediate to avoid overflow.
 */
static inline int32_t lerp_i32(int32_t s0, int32_t s1, uint32_t frac_q16)
{
    int64_t diff = (int64_t)s1 - (int64_t)s0;
    int64_t product = diff * (int64_t)frac_q16;
    return s0 + (int32_t)(product >> Q16_FRAC_BITS);
}

void audio_resampler_stream_init(audio_resampler_stream_t *rs,
                                  audio_sample_rate_t src_rate,
                                  audio_sample_rate_t dst_rate,
                                  audio_bit_depth_t bit_depth,
                                  int channels)
{
    rs->src_rate = src_rate;
    rs->dst_rate = dst_rate;
    rs->bit_depth = bit_depth;
    rs->channels = channels;
    rs->pos_q16 = 0;

    // Compute step_q16 = (src_rate / dst_rate) in Q16.16 format
    // Example: 44100/48000 = 0.91875 → 0x0000EB00
    //          48000/44100 = 1.088435 → 0x00011689
    uint64_t step_q16_64 = ((uint64_t)src_rate << Q16_FRAC_BITS) / dst_rate;
    rs->step_q16 = (uint32_t)step_q16_64;

    ESP_LOGI(TAG, "Resampler init: %d→%d Hz, %d-bit, %d ch, step=0x%08lx",
             (int)src_rate, (int)dst_rate, (int)bit_depth, channels, rs->step_q16);
}

size_t audio_resampler_stream_min_in_frames(const audio_resampler_stream_t *rs,
                                             size_t out_frames)
{
    // Compute position after producing out_frames
    // end_pos_q16 = pos_q16 + (out_frames * step_q16)
    uint64_t end_pos_q16 = (uint64_t)rs->pos_q16 + ((uint64_t)out_frames * rs->step_q16);

    // Convert to integer frames needed (round up to ensure we have enough)
    // Need: ceil(end_pos_q16 / Q16_ONE)
    //     = (end_pos_q16 + Q16_ONE - 1) >> Q16_FRAC_BITS
    size_t frames_needed = (size_t)((end_pos_q16 + Q16_ONE - 1) >> Q16_FRAC_BITS);

    // Add 1 for interpolation (need frame i+1 when interpolating at position i)
    return frames_needed + 1;
}

size_t audio_resampler_stream_process(audio_resampler_stream_t *rs,
                                       const uint8_t *in_buf,
                                       size_t in_frames,
                                       uint8_t *out_buf,
                                       size_t out_frames,
                                       size_t *in_frames_consumed)
{
    const int sample_bytes = (rs->bit_depth == AUDIO_BIT_DEPTH_16) ? 2 : 4;
    const int frame_bytes = sample_bytes * rs->channels;
    
    size_t out_idx = 0;
    uint32_t pos = rs->pos_q16;

    // Process output frames
    for (size_t i = 0; i < out_frames; i++) {
        uint32_t i0 = Q16_INT(pos);  // Integer part: source frame index
        uint32_t frac = Q16_FRAC(pos);  // Fractional part: interpolation weight

        // Check if we have enough input frames for interpolation
        if (i0 + 1 < in_frames) {
            // Interpolate each channel
            for (int ch = 0; ch < rs->channels; ch++) {
                if (rs->bit_depth == AUDIO_BIT_DEPTH_16) {
                    // 16-bit samples
                    const int16_t *in_i16 = (const int16_t *)in_buf;
                    int16_t *out_i16 = (int16_t *)out_buf;
                    
                    size_t s0_idx = i0 * rs->channels + ch;
                    size_t s1_idx = (i0 + 1) * rs->channels + ch;
                    
                    int16_t s0 = in_i16[s0_idx];
                    int16_t s1 = in_i16[s1_idx];
                    
                    out_i16[out_idx * rs->channels + ch] = lerp_i16(s0, s1, frac);
                } else {
                    // 32-bit samples
                    const int32_t *in_i32 = (const int32_t *)in_buf;
                    int32_t *out_i32 = (int32_t *)out_buf;
                    
                    size_t s0_idx = i0 * rs->channels + ch;
                    size_t s1_idx = (i0 + 1) * rs->channels + ch;
                    
                    int32_t s0 = in_i32[s0_idx];
                    int32_t s1 = in_i32[s1_idx];
                    
                    out_i32[out_idx * rs->channels + ch] = lerp_i32(s0, s1, frac);
                }
            }
        } else if (i0 < in_frames) {
            // EOF condition: only one frame left, copy without interpolation
            for (int ch = 0; ch < rs->channels; ch++) {
                if (rs->bit_depth == AUDIO_BIT_DEPTH_16) {
                    const int16_t *in_i16 = (const int16_t *)in_buf;
                    int16_t *out_i16 = (int16_t *)out_buf;
                    out_i16[out_idx * rs->channels + ch] = in_i16[i0 * rs->channels + ch];
                } else {
                    const int32_t *in_i32 = (const int32_t *)in_buf;
                    int32_t *out_i32 = (int32_t *)out_buf;
                    out_i32[out_idx * rs->channels + ch] = in_i32[i0 * rs->channels + ch];
                }
            }
        } else {
            // Beyond EOF: pad with zeros
            util_safe_memset(out_buf + (out_idx * frame_bytes), frame_bytes, 0, frame_bytes);
        }

        out_idx++;
        pos += rs->step_q16;
    }

    // Compute whole frames consumed from input (CODE_REVIEW6 P0-B: clamp to available)
    // CRITICAL: When padding zeros at EOF (i0 >= in_frames), pos_q16 advances beyond
    // available input. We MUST NOT report consuming more frames than provided, as this
    // causes pcm_stash_consume_frames() to fail with ESP_ERR_INVALID_SIZE, stopping
    // playback early.
    //
    // FIX: Clamp consumed frames to actual input available
    uint32_t pos_int = Q16_INT(pos);
    *in_frames_consumed = (pos_int < in_frames) ? pos_int : in_frames;

    // Reset position for next call (CODE_REVIEW6 P0-B: EOF-aware phase handling)
    // When we've consumed all input (at EOF), reset phase to zero to prevent
    // infinite zero-padding with ever-increasing position values.
    // Otherwise, preserve fractional remainder for accurate inter-block resampling.
    if (*in_frames_consumed >= in_frames) {
        // At EOF: consumed all input, reset phase to avoid cumulative drift
        rs->pos_q16 = 0;
    } else {
        // Mid-stream: preserve fractional part for next call
        // This maintains sub-frame accuracy across blocks
        rs->pos_q16 = Q16_FRAC(pos);
    }

    return out_frames;  // Always produce exactly what was requested
}
