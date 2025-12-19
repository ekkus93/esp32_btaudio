#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

float calculate_rms(int16_t *buffer, size_t samples, int offset, int stride);
int16_t calculate_peak(int16_t *buffer, size_t samples);
void generate_test_tone(int16_t *buffer, size_t samples, float frequency, float sample_rate, int16_t amplitude);
void generate_stereo_test_tone(int16_t *buffer, size_t frames, float left_freq, float right_freq, float sample_rate, int16_t amplitude);
bool compare_audio_buffers(int16_t *buffer1, int16_t *buffer2, size_t samples, int16_t tolerance);
esp_err_t test_convert_stereo_to_mono(int16_t *stereo_buffer, int16_t *mono_buffer, size_t stereo_samples);
esp_err_t test_convert_mono_to_stereo(int16_t *mono_buffer, int16_t *stereo_buffer, size_t mono_samples);
void test_convert_16bit_to_24bit(int16_t *src_buffer, uint8_t *dst_buffer, size_t samples);
void test_convert_24bit_to_16bit(uint8_t *src_buffer, int16_t *dst_buffer, size_t samples);
