/*
 * radio_resampler — pure streaming resampler (RADIO-2b). Converts decoded PCM
 * (s16 interleaved, any rate/channel count) to the I2S contract: 44100 Hz,
 * stereo, s16. Linear interpolation with cross-call continuity so successive
 * decoder frames join glitch-free; 44.1 kHz stereo is a memcpy fast path.
 * No ESP-IDF deps; host-tested.
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
    double   step;       /* input frames advanced per output frame */
    double   frac;       /* fractional position in [0,1) between prev and next */
    int16_t  prev_l, prev_r;
    bool     primed;
} radio_resampler_t;

/* (Re)configure for a decoder output format. Safe to call mid-stream on a
 * format change; resets continuity. channels clamped to 1..2. */
void radio_resampler_init(radio_resampler_t *r, int src_rate, int channels);

/* Convert up to `in_frames` input frames into stereo s16 output frames.
 * Writes at most `out_cap` output frames to `out` (interleaved L,R). Returns
 * the number of output frames written; sets *in_used to input frames consumed.
 * Call repeatedly across a stream. */
size_t radio_resampler_run(radio_resampler_t *r, const int16_t *in, size_t in_frames,
                           int16_t *out, size_t out_cap, size_t *in_used);

#ifdef __cplusplus
}
#endif
