#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * Calculate the RMS (Root Mean Square) value of a buffer
 * 
 * @param buffer Pointer to audio buffer
 * @param samples Number of samples in the buffer
 * @param offset Starting offset into buffer
 * @param stride Stride between samples (e.g., 2 for stereo with specific channel)
 * @return RMS value
 */
float calculate_rms(int16_t* buffer, size_t samples, int offset, int stride);

/**
 * Calculate the peak value of a buffer
 * 
 * @param buffer Pointer to audio buffer
 * @param samples Number of samples in buffer
 * @return Peak absolute value
 */
int16_t calculate_peak(int16_t* buffer, size_t samples);

/**
 * Generate a sine wave into a buffer
 * 
 * @param buffer Output buffer
 * @param samples Number of samples to generate
 * @param frequency Frequency in Hz
 * @param sample_rate Sample rate in Hz
 * @param amplitude Peak amplitude (0-32767)
 */
void generate_test_tone(int16_t* buffer, size_t samples, float frequency, 
                       float sample_rate, int16_t amplitude);

/**
 * Generate a stereo sine wave with different frequencies for left and right channels
 * 
 * @param buffer Output buffer (interleaved stereo)
 * @param frames Number of stereo frames to generate
 * @param left_freq Left channel frequency in Hz
 * @param right_freq Right channel frequency in Hz
 * @param sample_rate Sample rate in Hz
 * @param amplitude Peak amplitude (0-32767)
 */
void generate_stereo_test_tone(int16_t* buffer, size_t frames, 
                              float left_freq, float right_freq,
                              float sample_rate, int16_t amplitude);

/**
 * Compare two audio buffers with a tolerance for numerical error
 * 
 * @param buffer1 First buffer
 * @param buffer2 Second buffer
 * @param samples Number of samples to compare
 * @param tolerance Maximum allowed difference between samples
 * @return true if buffers match within tolerance
 */
bool compare_audio_buffers(int16_t* buffer1, int16_t* buffer2, size_t samples, int16_t tolerance);

/**
 * Test wrapper for stereo to mono conversion - using the actual I2S audio implementation
 */
esp_err_t test_convert_stereo_to_mono(int16_t* stereo_buffer, int16_t* mono_buffer, size_t stereo_samples);

/**
 * Test wrapper for mono to stereo conversion - using the actual I2S audio implementation
 */
esp_err_t test_convert_mono_to_stereo(int16_t* mono_buffer, int16_t* stereo_buffer, size_t mono_samples);

/**
 * Test wrappers for PCM format conversion - these simply call the actual implementations
 * but with appropriate parameter types
 */
void test_convert_16bit_to_24bit(int16_t* src_buffer, uint8_t* dst_buffer, size_t samples);
void test_convert_24bit_to_16bit(uint8_t* src_buffer, int16_t* dst_buffer, size_t samples);
