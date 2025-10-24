#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * Calculate RMS of audio buffer
 * 
 * @param buffer Audio buffer
 * @param samples Number of samples
 * @param offset Start offset
 * @param stride Sample stride (useful for stereo channels)
 * @return RMS value
 */
float calculate_rms(int16_t* buffer, size_t samples, int offset, int stride);

/**
 * Calculate peak value of audio buffer
 * 
 * @param buffer Audio buffer
 * @param samples Number of samples
 * @return Peak value
 */
int16_t calculate_peak(int16_t* buffer, size_t samples);

/**
 * Generate a sine wave test tone
 * 
 * @param buffer Buffer to fill
 * @param samples Number of samples
 * @param frequency Tone frequency
 * @param sample_rate Sample rate
 * @param amplitude Amplitude (16-bit max)
 */
void generate_test_tone(int16_t* buffer, size_t samples, float frequency, 
                       float sample_rate, int16_t amplitude);

/**
 * Generate a stereo test tone
 * 
 * @param buffer Buffer to fill
 * @param frames Number of stereo frames
 * @param left_freq Left channel frequency
 * @param right_freq Right channel frequency
 * @param sample_rate Sample rate
 * @param amplitude Amplitude (16-bit max)
 */
void generate_stereo_test_tone(int16_t* buffer, size_t frames, 
                              float left_freq, float right_freq,
                              float sample_rate, int16_t amplitude);

/**
 * Compare audio buffers with tolerance
 * 
 * @param buffer1 First buffer
 * @param buffer2 Second buffer
 * @param samples Number of samples
 * @param tolerance Acceptable difference
 * @return true if buffers match within tolerance
 */
bool compare_audio_buffers(int16_t* buffer1, int16_t* buffer2, size_t samples, int16_t tolerance);

/**
 * Convert stereo to mono
 * 
 * @param stereo_buffer Stereo buffer
 * @param mono_buffer Mono buffer
 * @param stereo_samples Number of stereo frames
 * @return ESP_OK on success
 */
esp_err_t test_convert_stereo_to_mono(int16_t* stereo_buffer, int16_t* mono_buffer, size_t stereo_samples);

/**
 * Convert mono to stereo
 * 
 * @param mono_buffer Mono buffer
 * @param stereo_buffer Stereo buffer
 * @param mono_samples Number of mono samples
 * @return ESP_OK on success
 */
esp_err_t test_convert_mono_to_stereo(int16_t* mono_buffer, int16_t* stereo_buffer, size_t mono_samples);

/**
 * Test wrappers for PCM format conversion - these simply call the actual implementations
 * but with appropriate parameter types
 */
void test_convert_16bit_to_24bit(int16_t* src_buffer, uint8_t* dst_buffer, size_t samples);
void test_convert_24bit_to_16bit(uint8_t* src_buffer, int16_t* dst_buffer, size_t samples);
