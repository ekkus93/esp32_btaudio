#include "hfp_voice_convert.h"

#include <limits.h>
#include <string.h>

static int16_t saturate_i16(int64_t value)
{
    if (value > INT16_MAX) return INT16_MAX;
    if (value < INT16_MIN) return INT16_MIN;
    return (int16_t)value;
}

size_t hfp_cvsd_8k_to_16k(const int16_t *src,
                          size_t src_samples,
                          int16_t *dst,
                          size_t dst_capacity_samples)
{
    if (src == NULL || dst == NULL || src_samples == 0U ||
        src_samples > SIZE_MAX / 2U ||
        dst_capacity_samples < src_samples * 2U) {
        return 0U;
    }
    for (size_t i = 0; i < src_samples; ++i) {
        dst[2U * i] = src[i];
        dst[2U * i + 1U] = src[i];
    }
    return src_samples * 2U;
}

size_t hfp_stereo16_to_mono(const int16_t *src_interleaved,
                            size_t src_frames,
                            int16_t *dst_mono,
                            size_t dst_capacity_frames)
{
    if (src_interleaved == NULL || dst_mono == NULL || src_frames == 0U ||
        src_frames > SIZE_MAX / 2U || dst_capacity_frames < src_frames) {
        return 0U;
    }
    for (size_t frame = 0; frame < src_frames; ++frame) {
        int32_t left = src_interleaved[2U * frame];
        int32_t right = src_interleaved[2U * frame + 1U];
        int64_t average = ((int64_t)left + right) / 2;
        dst_mono[frame] = saturate_i16(average);
    }
    return src_frames;
}

esp_err_t hfp_voice_rate_converter_init(hfp_voice_rate_converter_t *state,
                                        uint32_t input_rate_hz,
                                        uint32_t output_rate_hz)
{
    if (state == NULL || input_rate_hz == 0U ||
        (output_rate_hz != 8000U && output_rate_hz != 16000U) ||
        input_rate_hz < output_rate_hz) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(state, 0, sizeof(*state));
    state->input_rate_hz = input_rate_hz;
    state->output_rate_hz = output_rate_hz;
    state->initialized = true;
    return ESP_OK;
}

void hfp_voice_rate_converter_reset(hfp_voice_rate_converter_t *state)
{
    if (state == NULL || !state->initialized) return;
    state->phase = 0U;
    state->window_sum = 0;
    state->window_samples = 0U;
}

esp_err_t hfp_voice_rate_converter_process(
    hfp_voice_rate_converter_t *state,
    const int16_t *src,
    size_t src_samples,
    int16_t *dst,
    size_t dst_capacity_samples,
    size_t *src_consumed,
    size_t *dst_produced)
{
    if (src_consumed == NULL || dst_produced == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *src_consumed = 0U;
    *dst_produced = 0U;
    if (state == NULL || !state->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((src_samples > 0U && src == NULL) ||
        (dst_capacity_samples > 0U && dst == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (src_samples == 0U || dst_capacity_samples == 0U) {
        return ESP_OK;
    }

    size_t consumed = 0U;
    size_t produced = 0U;
    while (consumed < src_samples) {
        uint64_t next_phase =
            (uint64_t)state->phase + state->output_rate_hz;
        bool emits = next_phase >= state->input_rate_hz;
        if (emits && produced == dst_capacity_samples) {
            break;
        }

        state->window_sum += src[consumed];
        state->window_samples++;
        consumed++;

        if (emits) {
            int64_t average = state->window_sum /
                              (int64_t)state->window_samples;
            dst[produced++] = saturate_i16(average);
            state->window_sum = 0;
            state->window_samples = 0U;
            next_phase -= state->input_rate_hz;
        }
        state->phase = (uint32_t)next_phase;
    }

    *src_consumed = consumed;
    *dst_produced = produced;
    return ESP_OK;
}
