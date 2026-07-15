/* radio_resampler — streaming linear-interp resampler. See header. */
#include "radio_resampler.h"

#include <math.h>
#include <string.h>

static int16_t clamp_i16(long value)
{
    if (value > INT16_MAX) return INT16_MAX;
    if (value < INT16_MIN) return INT16_MIN;
    return (int16_t)value;
}

static inline void read_frame(const int16_t *in, size_t index, int channels,
                              int16_t *left, int16_t *right)
{
    if (channels == 1) {
        *left = in[index];
        *right = in[index];
    } else {
        *left = in[index * 2];
        *right = in[index * 2 + 1];
    }
}

bool radio_resampler_init(radio_resampler_t *r, int src_rate, int channels)
{
    if (!r) return false;
    if (src_rate <= 0 || (channels != 1 && channels != 2)) {
        if (r) memset(r, 0, sizeof(*r));
        return false;
    }

    memset(r, 0, sizeof(*r));
    r->src_rate = src_rate;
    r->channels = channels;
    r->step = (double)src_rate / (double)RESAMPLE_OUT_RATE;
    return isfinite(r->step) && r->step > 0.0;
}

size_t radio_resampler_run(radio_resampler_t *r, const int16_t *in, size_t in_frames,
                           int16_t *out, size_t out_cap, size_t *in_used)
{
    if (in_used) *in_used = 0;
    if (!r || !in || !out || !in_used || out_cap == 0 ||
        r->src_rate <= 0 || (r->channels != 1 && r->channels != 2)) {
        return 0;
    }

    size_t input_index = 0;
    size_t output_frames = 0;

    while (output_frames < out_cap) {
        if (!r->primed) {
            if (input_index >= in_frames) break;
            read_frame(in, input_index, r->channels, &r->left_l, &r->left_r);
            input_index++;
            r->phase = 0.0;
            r->primed = true;
        }

        while (r->phase >= 1.0) {
            if (input_index >= in_frames) {
                *in_used = input_index;
                return output_frames;
            }
            read_frame(in, input_index, r->channels, &r->left_l, &r->left_r);
            input_index++;
            r->phase -= 1.0;
        }

        int16_t right_l = r->left_l;
        int16_t right_r = r->left_r;
        if (r->phase > 0.0) {
            if (input_index >= in_frames) break;   /* need the next frame to interpolate */
            read_frame(in, input_index, r->channels, &right_l, &right_r);
        }

        double l = (double)r->left_l + ((double)right_l - (double)r->left_l) * r->phase;
        double rr = (double)r->left_r + ((double)right_r - (double)r->left_r) * r->phase;

        out[output_frames * 2] = clamp_i16(lround(l));
        out[output_frames * 2 + 1] = clamp_i16(lround(rr));
        output_frames++;
        r->phase += r->step;
    }

    *in_used = input_index;
    return output_frames;
}
