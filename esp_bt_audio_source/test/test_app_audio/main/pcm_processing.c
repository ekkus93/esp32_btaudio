#include "pcm_processing.h"

void pcm_convert_16bit_to_24bit(int16_t *src_buffer, int32_t *dst_buffer, size_t samples)
{
    if (!src_buffer || !dst_buffer) {
        return;
    }
    for (size_t i = 0; i < samples; i++) {
        dst_buffer[i] = ((int32_t)src_buffer[i]) << 8;
    }
}

void pcm_convert_24bit_to_16bit(int32_t *src_buffer, int16_t *dst_buffer, size_t samples)
{
    if (!src_buffer || !dst_buffer) {
        return;
    }
    for (size_t i = 0; i < samples; i++) {
        dst_buffer[i] = (int16_t)(src_buffer[i] >> 8);
    }
}
