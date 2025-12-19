#include "audio_test_helpers.h"
#include "i2s_audio.h"
#include "pcm_processing.h"
#include "esp_log.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "TEST_HELPERS";

float calculate_rms(int16_t *buffer, size_t samples, int offset, int stride)
{
    if (!buffer || samples == 0) {
        return 0.0f;
    }
    float sum_squares = 0.0f;
    size_t sample_count = 0;
    for (size_t i = offset; i < samples; i += stride) {
        float sample = buffer[i] / 32768.0f;
        sum_squares += sample * sample;
        sample_count++;
    }
    if (sample_count == 0) {
        return 0.0f;
    }
    return sqrtf(sum_squares / sample_count);
}

int16_t calculate_peak(int16_t *buffer, size_t samples)
{
    if (!buffer || samples == 0) {
        return 0;
    }
    int16_t peak = 0;
    for (size_t i = 0; i < samples; i++) {
        int16_t abs_val = abs(buffer[i]);
        if (abs_val > peak) {
            peak = abs_val;
        }
    }
    return peak;
}

void generate_test_tone(int16_t *buffer, size_t samples, float frequency, float sample_rate, int16_t amplitude)
{
    if (!buffer) {
        return;
    }
    for (size_t i = 0; i < samples; i++) {
        double angle = 2.0 * M_PI * frequency * i / sample_rate;
        buffer[i] = (int16_t)(amplitude * sin(angle));
    }
}

void generate_stereo_test_tone(int16_t *buffer, size_t frames, float left_freq, float right_freq, float sample_rate, int16_t amplitude)
{
    if (!buffer) {
        return;
    }
    for (size_t i = 0; i < frames; i++) {
        double sample_index = (double)(i + 1);
        double left_angle = 2.0 * M_PI * left_freq * sample_index / sample_rate;
        double right_angle = 2.0 * M_PI * right_freq * sample_index / sample_rate;
        buffer[i * 2] = (int16_t)(amplitude * sin(left_angle));
        buffer[i * 2 + 1] = (int16_t)(amplitude * sin(right_angle));
    }
}

bool compare_audio_buffers(int16_t *buffer1, int16_t *buffer2, size_t samples, int16_t tolerance)
{
    if (!buffer1 || !buffer2) {
        return false;
    }
    for (size_t i = 0; i < samples; i++) {
        int16_t diff = abs(buffer1[i] - buffer2[i]);
        if (diff > tolerance) {
            return false;
        }
    }
    return true;
}

esp_err_t test_convert_stereo_to_mono(int16_t *stereo_buffer, int16_t *mono_buffer, size_t stereo_samples)
{
    return i2s_convert_stereo_to_mono(stereo_buffer, mono_buffer, stereo_samples);
}

esp_err_t test_convert_mono_to_stereo(int16_t *mono_buffer, int16_t *stereo_buffer, size_t mono_samples)
{
    return i2s_convert_mono_to_stereo(mono_buffer, stereo_buffer, mono_samples);
}

void test_convert_16bit_to_24bit(int16_t *src_buffer, uint8_t *dst_buffer, size_t samples)
{
    if (!src_buffer || !dst_buffer || samples == 0) {
        return;
    }
    #define MAX_TEMP_BUFFER_SIZE 1024
    static int32_t static_temp_buffer[MAX_TEMP_BUFFER_SIZE];
    int32_t *temp_buffer = NULL;
    bool using_dynamic = false;
    if (samples <= MAX_TEMP_BUFFER_SIZE) {
        temp_buffer = static_temp_buffer;
    } else {
        temp_buffer = (int32_t *)malloc(samples * sizeof(int32_t));
        if (!temp_buffer) {
            ESP_LOGE(TAG, "Memory allocation failed for PCM conversion");
            return;
        }
        using_dynamic = true;
    }
    pcm_convert_16bit_to_24bit(src_buffer, temp_buffer, samples);
    for (size_t i = 0; i < samples; i++) {
        dst_buffer[i * 3] = (uint8_t)(temp_buffer[i] & 0xFF);
        dst_buffer[i * 3 + 1] = (uint8_t)((temp_buffer[i] >> 8) & 0xFF);
        dst_buffer[i * 3 + 2] = (uint8_t)((temp_buffer[i] >> 16) & 0xFF);
    }
    if (using_dynamic) {
        free(temp_buffer);
    }
}

void test_convert_24bit_to_16bit(uint8_t *src_buffer, int16_t *dst_buffer, size_t samples)
{
    if (!src_buffer || !dst_buffer || samples == 0) {
        return;
    }
    #define MAX_TEMP_BUFFER_SIZE 1024
    static int32_t static_temp_buffer[MAX_TEMP_BUFFER_SIZE];
    int32_t *temp_buffer = NULL;
    bool using_dynamic = false;
    if (samples <= MAX_TEMP_BUFFER_SIZE) {
        temp_buffer = static_temp_buffer;
    } else {
        temp_buffer = (int32_t *)malloc(samples * sizeof(int32_t));
        if (!temp_buffer) {
            ESP_LOGE(TAG, "Memory allocation failed for PCM conversion");
            return;
        }
        using_dynamic = true;
    }
    for (size_t i = 0; i < samples; i++) {
        temp_buffer[i] = (int32_t)src_buffer[i * 3] |
                         ((int32_t)src_buffer[i * 3 + 1] << 8) |
                         ((int32_t)src_buffer[i * 3 + 2] << 16);
        if (temp_buffer[i] & 0x800000) {
            temp_buffer[i] |= 0xFF000000;
        }
    }
    pcm_convert_24bit_to_16bit(temp_buffer, dst_buffer, samples);
    if (using_dynamic) {
        free(temp_buffer);
    }
}
