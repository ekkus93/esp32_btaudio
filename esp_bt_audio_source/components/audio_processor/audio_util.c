#include "audio_util.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "util_safe.h"

static const char *TAG = "audio_util";

static int bytes_per_sample(audio_bit_depth_t bit_depth)
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

esp_err_t convert_audio_format(const audio_convert_args_t *args)
{
    if (args == NULL || args->dst_size == NULL || args->dst == NULL || args->src == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const void *src = args->src;
    void *dst = args->dst;
    size_t src_size = args->src_size;
    const audio_bit_depth_t src_bit_depth = args->src_bit_depth;
    const audio_bit_depth_t dst_bit_depth = args->dst_bit_depth;
    size_t *dst_size = args->dst_size;
    size_t work_bytes = args->work_bytes ? args->work_bytes : SIZE_MAX;

    if (src_bit_depth == dst_bit_depth) {
        size_t copy_size = (src_size > work_bytes) ? work_bytes : src_size;
        util_safe_memmove(dst, work_bytes, src, copy_size);
        *dst_size = copy_size;
        return ESP_OK;
    }

    const int src_bytes_per_sample = bytes_per_sample(src_bit_depth);
    const int dst_bytes_per_sample = bytes_per_sample(dst_bit_depth);
    if (src_bytes_per_sample <= 0 || dst_bytes_per_sample <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int src_sample_count = (int)(src_size / (size_t)src_bytes_per_sample);
    size_t calculated = (size_t)src_sample_count * (size_t)dst_bytes_per_sample;
    if (calculated > work_bytes) {
        calculated = work_bytes;
    }
    *dst_size = calculated;

    if (src_bit_depth == AUDIO_BIT_DEPTH_16 && dst_bit_depth == AUDIO_BIT_DEPTH_32) {
        const int16_t *src_samples = (const int16_t *)src;
        int32_t *dst_samples = (int32_t *)dst;
        const int max_samples = (int)(*dst_size / sizeof(int32_t));
        for (int i = 0; i < src_sample_count && i < max_samples; ++i) {
            dst_samples[i] = ((int32_t)src_samples[i]) << 16;
        }
    } else if (src_bit_depth == AUDIO_BIT_DEPTH_32 && dst_bit_depth == AUDIO_BIT_DEPTH_16) {
        const int32_t *src_samples = (const int32_t *)src;
        int16_t *dst_samples = (int16_t *)dst;
        const int max_samples = (int)(*dst_size / sizeof(int16_t));
        for (int i = 0; i < src_sample_count && i < max_samples; ++i) {
            dst_samples[i] = (int16_t)(src_samples[i] >> 16);
        }
    } else if (src_bit_depth == AUDIO_BIT_DEPTH_24 && dst_bit_depth == AUDIO_BIT_DEPTH_16) {
        const int32_t *src_samples = (const int32_t *)src;
        int16_t *dst_samples = (int16_t *)dst;
        const int max_samples = (int)(*dst_size / sizeof(int16_t));
        for (int i = 0; i < src_sample_count && i < max_samples; ++i) {
            dst_samples[i] = (int16_t)(src_samples[i] >> 8);
        }
    } else if (src_bit_depth == AUDIO_BIT_DEPTH_16 && dst_bit_depth == AUDIO_BIT_DEPTH_24) {
        const int16_t *src_samples = (const int16_t *)src;
        int32_t *dst_samples = (int32_t *)dst;
        const int max_samples = (int)(*dst_size / sizeof(int32_t));
        for (int i = 0; i < src_sample_count && i < max_samples; ++i) {
            dst_samples[i] = ((int32_t)src_samples[i]) << 8;
        }
    } else if (src_bit_depth == AUDIO_BIT_DEPTH_16 && dst_bit_depth == AUDIO_BIT_DEPTH_16) {
        size_t copy_size = (src_size > work_bytes) ? work_bytes : src_size;
        util_safe_memmove(dst, work_bytes, src, copy_size);
        *dst_size = copy_size;
    } else {
        ESP_LOGE(TAG, "Unsupported format conversion: %d -> %d", src_bit_depth, dst_bit_depth);  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}

esp_err_t resample_audio(const audio_resample_args_t *args)
{
    if (args == NULL || args->dst_size == NULL || args->dst == NULL || args->src == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const void *src = args->src;
    void *dst = args->dst;
    size_t src_size = args->src_size;
    const audio_sample_rate_t src_rate = args->src_rate;
    const audio_sample_rate_t dst_rate = args->dst_rate;
    const audio_bit_depth_t bit_depth = args->bit_depth;
    const audio_channel_t channels_in = args->channels;
    size_t *dst_size = args->dst_size;
    size_t work_bytes = args->work_bytes;

    *dst_size = 0;

    if (src_rate <= 0 || dst_rate <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int channels = (channels_in == AUDIO_CHANNEL_MONO || channels_in == AUDIO_CHANNEL_STEREO)
                       ? (int)channels_in
                       : AUDIO_CHANNEL_STEREO;

    const int bps = bytes_per_sample(bit_depth);
    if (bps <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t frame_bytes = (size_t)bps * (size_t)channels;
    if (frame_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (work_bytes == 0 || work_bytes > src_size) {
        work_bytes = src_size;
    }
    if (src_size > work_bytes) {
        src_size = work_bytes;
    }

    if (src_size == 0) {
        return ESP_OK;
    }

    size_t src_sample_count = src_size / (size_t)bps;
    size_t src_frame_count = src_sample_count / (size_t)channels;
    if (src_frame_count == 0) {
        return ESP_OK;
    }

    if (src_rate == dst_rate) {
        util_safe_memmove(dst, work_bytes, src, src_size);
        *dst_size = src_size;
        return ESP_OK;
    }

    double ratio = (double)dst_rate / (double)src_rate;
    if (!(ratio > 0.0) || !isfinite(ratio)) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t max_dst_frames = work_bytes / frame_bytes;
    if (max_dst_frames == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t dst_frame_count = (size_t)floor((double)src_frame_count * ratio);
    if (dst_frame_count == 0) {
        dst_frame_count = 1;
    }
    if (dst_frame_count > max_dst_frames) {
        dst_frame_count = max_dst_frames;
    }

    size_t dst_sample_count = dst_frame_count * (size_t)channels;
    size_t dst_bytes = dst_sample_count * (size_t)bps;
    if (dst_bytes > work_bytes) {
        dst_bytes = work_bytes;
        dst_sample_count = dst_bytes / (size_t)bps;
        dst_frame_count = dst_sample_count / (size_t)channels;
    }

    if (dst_bytes == 0 || dst_frame_count == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (bit_depth == AUDIO_BIT_DEPTH_16) {
        const int16_t *src_samples = (const int16_t *)src;
        int16_t *dst_samples = (int16_t *)dst;
        for (size_t dst_frame = 0; dst_frame < dst_frame_count; ++dst_frame) {
            double t = (dst_frame_count > 1)
                           ? (double)dst_frame * (double)(src_frame_count - 1) / (double)(dst_frame_count - 1)
                           : 0.0;
            size_t src_frame_0 = (size_t)floor(t);
            double frac = t - (double)src_frame_0;
            size_t src_frame_1 = (src_frame_0 + 1 < src_frame_count) ? (src_frame_0 + 1) : src_frame_0;

            for (int ch = 0; ch < channels; ++ch) {
                size_t src_idx1 = (src_frame_0 * (size_t)channels) + (size_t)ch;
                size_t src_idx2 = (src_frame_1 * (size_t)channels) + (size_t)ch;
                size_t dst_idx = (dst_frame * (size_t)channels) + (size_t)ch;

                if (dst_idx >= dst_sample_count || src_idx2 >= src_sample_count) {
                    continue;
                }

                if (src_frame_1 == src_frame_0) {
                    dst_samples[dst_idx] = src_samples[src_idx1];
                } else {
                    dst_samples[dst_idx] = (int16_t)(((1.0 - frac) * (double)src_samples[src_idx1]) + (frac * (double)src_samples[src_idx2]));
                }
            }
        }
    } else {
        const int32_t *src_samples = (const int32_t *)src;
        int32_t *dst_samples = (int32_t *)dst;
        for (size_t dst_frame = 0; dst_frame < dst_frame_count; ++dst_frame) {
            double t = (dst_frame_count > 1)
                           ? (double)dst_frame * (double)(src_frame_count - 1) / (double)(dst_frame_count - 1)
                           : 0.0;
            size_t src_frame_0 = (size_t)floor(t);
            double frac = t - (double)src_frame_0;
            size_t src_frame_1 = (src_frame_0 + 1 < src_frame_count) ? (src_frame_0 + 1) : src_frame_0;

            for (int ch = 0; ch < channels; ++ch) {
                size_t src_idx1 = (src_frame_0 * (size_t)channels) + (size_t)ch;
                size_t src_idx2 = (src_frame_1 * (size_t)channels) + (size_t)ch;
                size_t dst_idx = (dst_frame * (size_t)channels) + (size_t)ch;

                if (dst_idx >= dst_sample_count || src_idx2 >= src_sample_count) {
                    continue;
                }

                if (src_frame_1 == src_frame_0) {
                    dst_samples[dst_idx] = src_samples[src_idx1];
                } else {
                    dst_samples[dst_idx] = (int32_t)(((1.0 - frac) * (double)src_samples[src_idx1]) + (frac * (double)src_samples[src_idx2]));
                }
            }
        }
    }

    *dst_size = dst_frame_count * frame_bytes;
    return ESP_OK;
}
