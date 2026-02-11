#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * Convert 16-bit PCM to 24-bit PCM
 * 
 * @param src_buffer Source buffer (16-bit)
 * @param dst_buffer Destination buffer (24-bit in 32-bit container)
 * @param samples Number of samples
 */
void pcm_convert_16bit_to_24bit(int16_t* src_buffer, int32_t* dst_buffer, size_t samples);

/**
 * Convert 24-bit PCM to 16-bit PCM
 * 
 * @param src_buffer Source buffer (24-bit in 32-bit container)
 * @param dst_buffer Destination buffer (16-bit)
 * @param samples Number of samples
 */
void pcm_convert_24bit_to_16bit(int32_t* src_buffer, int16_t* dst_buffer, size_t samples);
