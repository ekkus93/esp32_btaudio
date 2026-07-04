/**
 * uart_source.h — UART audio source for the audio processor
 *
 * Receives stereo 22050 Hz s16le PCM (pushed by the UART reader task),
 * upsamples it 2x to 44100 Hz and serves it to the audio engine as
 * AUDIO_SOURCE_UART. Lifecycle/staging-ring API arrives with the source
 * implementation; this header currently exposes the pure upsampler.
 */

#ifndef UART_SOURCE_H
#define UART_SOURCE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 2x linear upsampler for interleaved stereo s16 PCM (22050 -> 44100 Hz).
 *
 * For each input frame (L,R), emits two output frames per channel:
 * the midpoint (prev + cur) / 2 (C truncation), then cur. prev[2]
 * ({L,R}) carries the last input frame across calls so split feeds are
 * seamless; reset it to the first sample value (or zero) at stream start.
 *
 * in:  in_frames stereo frames (2*in_frames int16 samples)
 * out: must hold 2*in_frames stereo frames (4*in_frames int16 samples)
 */
void audio_upsample2x_s16_stereo(const int16_t *in, size_t in_frames,
                                 int16_t *out, int16_t prev[2]);

#ifdef __cplusplus
}
#endif

#endif /* UART_SOURCE_H */
