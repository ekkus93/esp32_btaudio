#ifndef AUDIO_TEST_HELPERS_H
#define AUDIO_TEST_HELPERS_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
// Need math.h for math functions used in implementation
#include <math.h>

// Audio test tone generation
void generate_test_tone(int16_t* buffer, size_t samples, float frequency, float amplitude, int sample_rate);
void generate_stereo_test_tone(int16_t* buffer, size_t frames, float left_freq, float right_freq, float amplitude, int sample_rate);

// Audio analysis functions
float calculate_rms(int16_t* buffer, size_t samples);
bool compare_audio_buffers(int16_t* buffer1, int16_t* buffer2, size_t samples, float tolerance);

// Format conversion helpers
void convert_16bit_to_24bit(int16_t* src, int32_t* dst, size_t samples);
void convert_24bit_to_16bit(int32_t* src, int16_t* dst, size_t samples);
void convert_mono_to_stereo(int16_t* src, int16_t* dst, size_t mono_samples);
void convert_stereo_to_mono(int16_t* src, int16_t* dst, size_t stereo_frames);
void apply_volume(int16_t* buffer, size_t samples, float volume);

#endif // AUDIO_TEST_HELPERS_H
