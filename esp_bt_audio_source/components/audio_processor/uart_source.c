/**
 * uart_source.c — UART audio source for the audio processor
 *
 * Step 2 of the UARTAUDIO feature: the pure 2x upsampler. The staging
 * ring / lifecycle / fill path (uart_source_start/write/fill/stop) is
 * added next; keeping this file dependency-free until then lets the
 * upsampler be host-tested standalone.
 */

#include "uart_source.h"

void audio_upsample2x_s16_stereo(const int16_t *in, size_t in_frames,
                                 int16_t *out, int16_t prev[2])
{
    int32_t prev_l = prev[0];
    int32_t prev_r = prev[1];

    for (size_t i = 0; i < in_frames; i++) {
        const int32_t cur_l = in[2U * i];
        const int32_t cur_r = in[(2U * i) + 1U];

        out[4U * i] = (int16_t)((prev_l + cur_l) / 2);
        out[(4U * i) + 1U] = (int16_t)((prev_r + cur_r) / 2);
        out[(4U * i) + 2U] = (int16_t)cur_l;
        out[(4U * i) + 3U] = (int16_t)cur_r;

        prev_l = cur_l;
        prev_r = cur_r;
    }

    prev[0] = (int16_t)prev_l;
    prev[1] = (int16_t)prev_r;
}
