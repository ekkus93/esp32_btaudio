/**
 * @file audio_resampler_stream.h
 * @brief Stateful streaming resampler with Q16.16 fixed-point phase accumulation
 *
 * WHY: Block-local resampling without phase carry causes cumulative frame loss
 * HOW: Maintains fractional position across blocks using Q16.16 fixed-point
 * CORRECTNESS: Always produces exactly the requested output frames; no loss
 *
 * Design:
 * - Fixed output chunk size (caller determines frames needed)
 * - Variable input reads (computed from ratio and phase)
 * - Linear interpolation between adjacent samples
 * - Phase accumulator prevents rounding loss
 *
 * Usage pattern:
 *   1. audio_resampler_stream_init() - initialize with source/dest rates
 *   2. Loop:
 *      a. audio_resampler_stream_min_in_frames() - compute input needed
 *      b. Ensure input buffer has enough frames
 *      c. audio_resampler_stream_process() - resample one chunk
 *   3. State persists across calls (phase carries)
 */

#ifndef AUDIO_RESAMPLER_STREAM_H
#define AUDIO_RESAMPLER_STREAM_H

#include "audio_processor.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Streaming resampler state
 *
 * Maintains fractional position using Q16.16 fixed-point arithmetic:
 * - Integer part: current source frame index
 * - Fractional part: interpolation position between frames
 *
 * Example: pos_q16 = 0x00012800 means position 1.15625
 * (1 whole frame + 0x2800/0x10000 fractional part)
 */
typedef struct {
    audio_sample_rate_t src_rate;   ///< Source sample rate
    audio_sample_rate_t dst_rate;   ///< Destination sample rate
    audio_bit_depth_t bit_depth;    ///< Sample bit depth (16 or 32)
    int channels;                   ///< Number of channels (1=mono, 2=stereo)
    uint32_t pos_q16;               ///< Current position in Q16.16 format
    uint32_t step_q16;              ///< Increment per output frame (Q16.16)
} audio_resampler_stream_t;

/**
 * @brief Initialize streaming resampler
 *
 * Computes Q16.16 step size from sample rate ratio.
 * Resets position to 0.
 *
 * @param rs Resampler state to initialize
 * @param src_rate Source sample rate
 * @param dst_rate Destination sample rate
 * @param bit_depth Sample bit depth (AUDIO_BIT_DEPTH_16 or AUDIO_BIT_DEPTH_32)
 * @param channels Number of channels (1 or 2)
 *
 * @note step_q16 = (src_rate << 16) / dst_rate
 *       For 44.1kHz→48kHz: step = 0x0001_1689 (≈1.088435)
 */
void audio_resampler_stream_init(audio_resampler_stream_t *rs,
                                  audio_sample_rate_t src_rate,
                                  audio_sample_rate_t dst_rate,
                                  audio_bit_depth_t bit_depth,
                                  int channels);

/**
 * @brief Compute minimum input frames needed for requested output
 *
 * Accounts for current fractional position to determine how many
 * source frames must be available.
 *
 * WHY: Variable input requirement depends on phase and ratio
 * EXAMPLE: Upsampling 44.1k→48k, requesting 256 output frames
 *          might need 235-236 input frames depending on pos_q16
 *
 * @param rs Resampler state (current position matters)
 * @param out_frames Desired output frame count
 * @return Minimum source frames required (rounded up)
 *
 * @note Formula: ceil((out_frames * step_q16 + pos_q16) / 65536)
 */
size_t audio_resampler_stream_min_in_frames(const audio_resampler_stream_t *rs,
                                             size_t out_frames);

/**
 * @brief Process one resampling chunk
 *
 * Produces exactly out_frames using linear interpolation.
 * Updates pos_q16 to carry fractional position to next call.
 *
 * Algorithm (per output sample):
 *   1. i0 = pos_q16 >> 16 (integer part)
 *   2. frac = pos_q16 & 0xFFFF (fractional part)
 *   3. y = (1 - frac/65536) * in[i0] + (frac/65536) * in[i0+1]
 *   4. pos_q16 += step_q16
 *
 * EOF handling:
 * - If input exhausted before out_frames produced, pads with zeros
 * - Caller should stop calling after EOF
 *
 * @param rs Resampler state (pos_q16 updated)
 * @param in_buf Input PCM buffer (interleaved if stereo)
 * @param in_frames Available input frames
 * @param out_buf Output PCM buffer (same format as input)
 * @param out_frames Desired output frames (MUST produce exactly this many)
 * @param in_frames_consumed [OUT] Whole input frames consumed
 *
 * @return Number of output frames produced (always == out_frames)
 *
 * @note in_frames_consumed may be less than in_frames (fractional carry)
 * @note Caller should remove in_frames_consumed from input buffer after call
 */
size_t audio_resampler_stream_process(audio_resampler_stream_t *rs,
                                       const uint8_t *in_buf,
                                       size_t in_frames,
                                       uint8_t *out_buf,
                                       size_t out_frames,
                                       size_t *in_frames_consumed);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_RESAMPLER_STREAM_H
