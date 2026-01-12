#pragma once

#include <stdint.h>
#include <stddef.h>

void pcm_convert_16bit_to_24bit(int16_t *src_buffer, int32_t *dst_buffer, size_t samples);
void pcm_convert_24bit_to_16bit(int32_t *src_buffer, int16_t *dst_buffer, size_t samples);
