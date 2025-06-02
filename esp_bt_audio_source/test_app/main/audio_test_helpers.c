#include "audio_test_helpers.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

// Define M_PI if not already defined
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "AUDIO_TEST_HELPERS";

// Generate a sine wave test tone
void generate_test_tone(int16_t* buffer, size_t samples, float frequency, float amplitude, int sample_rate) {
    if (!buffer) return;
    
    for (size_t i = 0; i < samples; i++) {
        float t = (float)i / sample_rate;  // Time in seconds
        float angle = 2.0f * (float)M_PI * frequency * t;
        buffer[i] = (int16_t)(amplitude * sinf(angle));
    }
}

// Generate sine wave stereo test tone (interleaved L/R)
void generate_stereo_test_tone(int16_t* buffer, size_t frames, float left_freq, float right_freq, float amplitude, int sample_rate) {
    if (!buffer) return;
    
    for (size_t i = 0; i < frames; i++) {
        float t = (float)i / sample_rate;  // Time in seconds
        float left_angle = 2.0f * M_PI * left_freq * t;
        float right_angle = 2.0f * M_PI * right_freq * t;
        
        // Interleaved L/R samples
        buffer[i*2] = (int16_t)(amplitude * sinf(left_angle));
        buffer[i*2+1] = (int16_t)(amplitude * sinf(right_angle));
    }
}

// Calculate RMS (Root Mean Square) of a buffer
float calculate_rms(int16_t* buffer, size_t samples) {
    if (!buffer || samples == 0) return 0.0f;
    
    int64_t sum = 0;
    
    for (size_t i = 0; i < samples; i++) {
        int32_t sample = buffer[i];
        sum += ((int64_t)sample * (int64_t)sample);
    }
    
    float rms = sqrtf((float)sum / samples);
    return rms;
}

// Compare audio buffers with tolerance
bool compare_audio_buffers(int16_t* buffer1, int16_t* buffer2, size_t samples, float tolerance) {
    if (!buffer1 || !buffer2 || samples == 0) return false;
    
    for (size_t i = 0; i < samples; i++) {
        float diff = fabsf((float)buffer1[i] - (float)buffer2[i]);
        float relative = (buffer1[i] != 0) ? diff / fabsf((float)buffer1[i]) : diff;
        
        if (relative > tolerance) {
            ESP_LOGE(TAG, "Buffer mismatch at sample %d: %d vs %d (diff: %.2f, rel: %.4f)",
                    (int)i, buffer1[i], buffer2[i], diff, relative);
            return false;
        }
    }
    
    return true;
}

// Convert bit depth (16-bit to 24-bit packed in 32-bit)
void convert_16bit_to_24bit(int16_t* src, int32_t* dst, size_t samples) {
    if (!src || !dst) return;
    
    for (size_t i = 0; i < samples; i++) {
        // Shift left by 8 bits to convert 16-bit to 24-bit
        dst[i] = ((int32_t)src[i]) << 8;
    }
}

// Convert bit depth (24-bit packed in 32-bit to 16-bit)
void convert_24bit_to_16bit(int32_t* src, int16_t* dst, size_t samples) {
    if (!src || !dst) return;
    
    for (size_t i = 0; i < samples; i++) {
        // Shift right by 8 bits and truncate to convert 24-bit to 16-bit
        dst[i] = (int16_t)(src[i] >> 8);
    }
}

// Convert mono to stereo (duplicate samples)
void convert_mono_to_stereo(int16_t* src, int16_t* dst, size_t mono_samples) {
    if (!src || !dst) return;
    
    for (size_t i = 0; i < mono_samples; i++) {
        // Duplicate each sample to left and right channels
        dst[i*2] = src[i];     // Left
        dst[i*2+1] = src[i];   // Right
    }
}

// Convert stereo to mono (average samples)
void convert_stereo_to_mono(int16_t* src, int16_t* dst, size_t stereo_frames) {
    if (!src || !dst) return;
    
    for (size_t i = 0; i < stereo_frames; i++) {
        // Average left and right channels
        dst[i] = (int16_t)(((int32_t)src[i*2] + (int32_t)src[i*2+1]) / 2);
    }
}

// Apply volume to samples
void apply_volume(int16_t* buffer, size_t samples, float volume) {
    if (!buffer) return;
    
    for (size_t i = 0; i < samples; i++) {
        buffer[i] = (int16_t)((float)buffer[i] * volume);
    }
}
