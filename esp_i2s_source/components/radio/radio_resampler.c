/* radio_resampler — streaming linear-interp resampler. See header. */
#include "radio_resampler.h"

#include <string.h>

void radio_resampler_init(radio_resampler_t *r, int src_rate, int channels)
{
    r->src_rate = src_rate > 0 ? src_rate : RESAMPLE_OUT_RATE;
    r->channels = (channels == 1) ? 1 : 2;
    r->step = (double)r->src_rate / (double)RESAMPLE_OUT_RATE;
    r->frac = 0.0;
    r->prev_l = r->prev_r = 0;
    r->primed = false;
}

static inline void get_frame(const int16_t *in, int channels, size_t i, int16_t *l, int16_t *r)
{
    if (channels == 1) {
        *l = *r = in[i];
    } else {
        *l = in[2 * i];
        *r = in[2 * i + 1];
    }
}

size_t radio_resampler_run(radio_resampler_t *r, const int16_t *in, size_t in_frames,
                           int16_t *out, size_t out_cap, size_t *in_used)
{
    if (in_used) *in_used = 0;
    if (!in || in_frames == 0 || !out || out_cap == 0) return 0;

    /* Fast path: already the target format. */
    if (r->src_rate == RESAMPLE_OUT_RATE && r->channels == 2) {
        size_t n = in_frames < out_cap ? in_frames : out_cap;
        memcpy(out, in, n * 2 * sizeof(int16_t));
        if (in_used) *in_used = n;
        return n;
    }

    if (!r->primed) {
        get_frame(in, r->channels, 0, &r->prev_l, &r->prev_r);
        r->primed = true;
    }

    size_t i = 0;      /* index of the "right" input frame */
    size_t o = 0;      /* output frames written */
    while (o < out_cap && i < in_frames) {
        int16_t rl, rr;
        get_frame(in, r->channels, i, &rl, &rr);
        double f = r->frac;
        out[2 * o]     = (int16_t)(r->prev_l + f * (rl - r->prev_l));
        out[2 * o + 1] = (int16_t)(r->prev_r + f * (rr - r->prev_r));
        o++;

        r->frac += r->step;
        while (r->frac >= 1.0 && i < in_frames) {
            r->frac -= 1.0;
            get_frame(in, r->channels, i, &r->prev_l, &r->prev_r);
            i++;
        }
    }

    if (in_used) *in_used = i;
    return o;
}
