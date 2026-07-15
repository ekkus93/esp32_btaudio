/*
 * radio_resampler — pure streaming resampler (RADIO-2b). Converts decoded PCM
 * (s16 interleaved, any rate/channel count) to the I2S contract: 44100 Hz,
 * stereo, s16. Linear interpolation with cross-call continuity so successive
 * decoder frames join glitch-free. No ESP-IDF deps; host-tested.
 *
 * RESAMPLE-001: the previous prev/frac bookkeeping used the wrong interval
 * and produced a mathematically wrong waveform, undetected because the old
 * tests only checked output counts and constant (DC) signals. This state
 * shape and algorithm (TODO Phase 6) is verified against an exact reference
 * ramp, chunk-boundary equivalence, and sine-frequency tests.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RESAMPLE_OUT_RATE  44100

typedef struct {
    int      src_rate;   /* input sample rate (Hz) */
    int      channels;   /* input channels: 1 or 2 */
    double   step;        /* source frames per output frame */
    double   phase;        /* position within the current source interval, may
                            * temporarily be >=1 while waiting for more input */
    int16_t  left_l, left_r;  /* left edge of the current interpolation interval */
    bool     primed;
} radio_resampler_t;

/* (Re)configure for a decoder output format. Safe to call mid-stream on a
 * format change; resets continuity. Returns false (and leaves *r zeroed) for
 * an invalid src_rate/channels rather than silently substituting a default
 * (RESAMPLE-002). */
bool radio_resampler_init(radio_resampler_t *r, int src_rate, int channels);

/* Convert up to `in_frames` input frames into stereo s16 output frames.
 * Writes at most `out_cap` output frames to `out` (interleaved L,R). Returns
 * the number of output frames written; sets *in_used to input frames
 * consumed. If *in_used < in_frames, call again with the unconsumed suffix
 * before discarding the decoder's output buffer — the remainder is still
 * needed to produce further output frames. */
size_t radio_resampler_run(radio_resampler_t *r, const int16_t *in, size_t in_frames,
                           int16_t *out, size_t out_cap, size_t *in_used);

#ifdef __cplusplus
}
#endif
