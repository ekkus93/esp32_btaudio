#include "audio_test_helpers.h"
#include "i2s_audio.h"
#include "pcm_processing.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Calculate RMS (Root Mean Square) of audio buffer
float calculate_rms(int16_t* buffer, size_t samples, int offset, int stride) {
    if (!buffer || samples == 0) return 0.0f;
    
    float sum_squares = 0.0f;
    size_t sample_count = 0;
    
    for (size_t i = offset; i < samples; i += stride) {
        float sample = buffer[i] / 32768.0f; // Normalize to -1.0 to 1.0
        sum_squares += sample * sample;
        sample_count++;
    }
    
    if (sample_count == 0) return 0.0f;
    
    return sqrtf(sum_squares / sample_count);
}

// Calculate peak value of audio buffer
int16_t calculate_peak(int16_t* buffer, size_t samples) {
    if (!buffer || samples == 0) return 0;
    
    int16_t peak = 0;
    
    for (size_t i = 0; i < samples; i++) {
        int16_t abs_val = abs(buffer[i]);
        if (abs_val > peak) {
            peak = abs_val;
        }
    }
    
    return peak;
}

// Generate a sine wave test tone
void generate_test_tone(int16_t* buffer, size_t samples, float frequency, 
                       float sample_rate, int16_t amplitude) {
    if (!buffer) return;
    
    for (size_t i = 0; i < samples; i++) {
        double angle = 2.0 * M_PI * frequency * i / sample_rate;
        buffer[i] = (int16_t)(amplitude * sin(angle));
    }
}

// Generate a stereo test tone with different frequencies for left and right channels
void generate_stereo_test_tone(int16_t* buffer, size_t frames, 
                              float left_freq, float right_freq,
                              float sample_rate, int16_t amplitude) {
    if (!buffer) return;
    
    for (size_t i = 0; i < frames; i++) {
        double left_angle = 2.0 * M_PI * left_freq * i / sample_rate;
        double right_angle = 2.0 * M_PI * right_freq * i / sample_rate;
        
        buffer[i * 2] = (int16_t)(amplitude * sin(left_angle));       // Left channel
        buffer[i * 2 + 1] = (int16_t)(amplitude * sin(right_angle));  // Right channel
    }
}

// Compare audio buffers with tolerance
bool compare_audio_buffers(int16_t* buffer1, int16_t* buffer2, size_t samples, int16_t tolerance) {
    if (!buffer1 || !buffer2) return false;
    
    for (size_t i = 0; i < samples; i++) {
        int16_t diff = abs(buffer1[i] - buffer2[i]);
        if (diff > tolerance) {
            return false;
        }
    }
    
    return true;
}

// Wrapper for stereo to mono conversion
esp_err_t test_convert_stereo_to_mono(int16_t* stereo_buffer, int16_t* mono_buffer, size_t stereo_samples) {
    // Call the actual implementation from i2s_audio.h
    return i2s_convert_stereo_to_mono(stereo_buffer, mono_buffer, stereo_samples);
}

// Wrapper for mono to stereo conversion
esp_err_t test_convert_mono_to_stereo(int16_t* mono_buffer, int16_t* stereo_buffer, size_t mono_samples) {
    // Call the actual implementation from i2s_audio.h
    return i2s_convert_mono_to_stereo(mono_buffer, stereo_buffer, mono_samples);
}

// Wrapper for 16-bit to 24-bit PCM conversion
void test_convert_16bit_to_24bit(int16_t* src_buffer, uint8_t* dst_buffer, size_t samples) {
    // We need to convert between the different parameter types expected by pcm_processing.h
    
    // Allocate temporary buffer for 32-bit int representation
    int32_t* temp_buffer = (int32_t*)malloc(samples * sizeof(int32_t));
    if (!temp_buffer) return;
    
    // Call the actual implementation
    pcm_convert_16bit_to_24bit(src_buffer, temp_buffer, samples);
    
    // Convert int32_t to byte array (3 bytes per sample)
    for (size_t i = 0; i < samples; i++) {
        // Extract the 24 bits (3 bytes) from the 32-bit value
        dst_buffer[i*3]     = (uint8_t)(temp_buffer[i] & 0xFF);
        dst_buffer[i*3 + 1] = (uint8_t)((temp_buffer[i] >> 8) & 0xFF);
        dst_buffer[i*3 + 2] = (uint8_t)((temp_buffer[i] >> 16) & 0xFF);
    }
    
    free(temp_buffer);
}

// Wrapper for 24-bit to 16-bit PCM conversion
void test_convert_24bit_to_16bit(uint8_t* src_buffer, int16_t* dst_buffer, size_t samples) {
    // We need to convert between the different parameter types expected by pcm_processing.h
    
    // Allocate temporary buffer for 32-bit int representation
    int32_t* temp_buffer = (int32_t*)malloc(samples * sizeof(int32_t));
    if (!temp_buffer) return;
    
    // Convert byte array to int32_t representation
    for (size_t i = 0; i < samples; i++) {
        // Combine the 3 bytes into a 24-bit value in a 32-bit container
        temp_buffer[i] = (int32_t)src_buffer[i*3] | 
                        ((int32_t)src_buffer[i*3 + 1] << 8) | 
                        ((int32_t)src_buffer[i*3 + 2] << 16);
                        
        // Sign extend if the 24-bit value is negative (MSB is 1)
        if (temp_buffer[i] & 0x800000) {
            temp_buffer[i] |= 0xFF000000;
        }
    }
    
    // Call the actual implementation
    pcm_convert_24bit_to_16bit(temp_buffer, dst_buffer, samples);
    
    free(temp_buffer);
}
