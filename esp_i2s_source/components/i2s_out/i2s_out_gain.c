/*
 * i2s_out_gain — pure pre-I2S software volume (host-tested).
 *
 * The audio_out feeder applies this to the mixed PCM (radio/tone) BEFORE the
 * 16-in-32 pack and I2S write, so the S3 can trim its source level independent
 * of the WROOM32's post-mix VOLUME. Kept in its own leaf file (no IDF deps) so
 * it compiles into both the device component and the host test. See i2s_out.h.
 */
#include "i2s_out.h"

void i2s_out_apply_gain(int16_t *samples, size_t count, int pct)
{
    if (!samples || count == 0 || pct >= 100) {
        return;                     /* >=100: unity, no-op */
    }
    if (pct <= 0) {                 /* <=0: mute */
        for (size_t i = 0; i < count; i++) {
            samples[i] = 0;
        }
        return;
    }
    for (size_t i = 0; i < count; i++) {
        /* Truncate toward zero — symmetric for +/- (a rounding bias would make
         * -1000 -> -499 instead of -500). Attenuation only, so no clip needed,
         * but keep the guard for safety. */
        int32_t v = ((int32_t)samples[i] * pct) / 100;
        if (v > 32767) {
            v = 32767;
        } else if (v < -32768) {
            v = -32768;
        }
        samples[i] = (int16_t)v;
    }
}
