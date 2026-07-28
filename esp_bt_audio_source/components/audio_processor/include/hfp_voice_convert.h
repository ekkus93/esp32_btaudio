#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t input_rate_hz;
    uint32_t output_rate_hz;
    uint32_t phase;
    int64_t window_sum;
    uint32_t window_samples;
    bool initialized;
} hfp_voice_rate_converter_t;

/* All-or-nothing helpers: return zero for invalid arguments or insufficient
 * destination capacity. */
size_t hfp_cvsd_8k_to_16k(const int16_t *src,
                          size_t src_samples,
                          int16_t *dst,
                          size_t dst_capacity_samples);

size_t hfp_stereo16_to_mono(const int16_t *src_interleaved,
                            size_t src_frames,
                            int16_t *dst_mono,
                            size_t dst_capacity_frames);

/* Stateful input-driven down-converter. It emits boxcar-averaged mono samples
 * at 8 kHz or 16 kHz, never fabricates samples, and preserves partial windows
 * across calls. input_rate_hz must be >= output_rate_hz. */
esp_err_t hfp_voice_rate_converter_init(hfp_voice_rate_converter_t *state,
                                        uint32_t input_rate_hz,
                                        uint32_t output_rate_hz);

void hfp_voice_rate_converter_reset(hfp_voice_rate_converter_t *state);

esp_err_t hfp_voice_rate_converter_process(
    hfp_voice_rate_converter_t *state,
    const int16_t *src,
    size_t src_samples,
    int16_t *dst,
    size_t dst_capacity_samples,
    size_t *src_consumed,
    size_t *dst_produced);

#ifdef __cplusplus
}
#endif
