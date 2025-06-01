#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

#include "audio_pipeline.h"
#include "pcm_format_test.h"

static const char *TAG = "PCM_FORMAT_TEST";

// Test helper function for PCM bit depth conversion
static esp_err_t test_pcm_bit_depth_convert(audio_buffer_t *in_buffer,
                                           audio_buffer_t *out_buffer,
                                           void *user_data)
{
    if (!in_buffer || !out_buffer || !user_data) {
        return ESP_ERR_INVALID_ARG;
    }

    pcm_convert_cfg_t *cfg = (pcm_convert_cfg_t *)user_data;
    uint8_t src_bit_depth = cfg->src_bit_depth;
    uint8_t dst_bit_depth = cfg->dst_bit_depth;
    
    // Validate bit depths
    if (src_bit_depth != 16 && src_bit_depth != 24 && src_bit_depth != 32) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (dst_bit_depth != 16 && dst_bit_depth != 24 && dst_bit_depth != 32) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Calculate bytes per sample for source and destination
    uint8_t src_bytes = src_bit_depth / 8;
    uint8_t dst_bytes = dst_bit_depth / 8;
    
    // Check if output buffer is large enough
    size_t src_samples = in_buffer->data_size / src_bytes;
    size_t dst_size = src_samples * dst_bytes;
    
    if (dst_size > out_buffer->size) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    uint8_t *src_data = (uint8_t *)in_buffer->data;
    uint8_t *dst_data = (uint8_t *)out_buffer->data;
    
    // Perform the conversion
    for (size_t i = 0; i < src_samples; i++) {
        // Extract the sample from the source buffer
        int32_t sample = 0;
        
        // Handle different source bit depths
        if (src_bit_depth == 16) {
            int16_t *src_samples = (int16_t *)src_data;
            sample = src_samples[i];
            // Scale up to 32-bit for processing
            sample = sample << 16;
        } else if (src_bit_depth == 24) {
            // 24-bit is typically stored in 3 bytes
            uint8_t *byte_ptr = src_data + (i * 3);
            // Need to handle endianness correctly
            if (cfg->is_big_endian) {
                sample = (byte_ptr[0] << 16) | (byte_ptr[1] << 8) | byte_ptr[2];
            } else {
                sample = (byte_ptr[2] << 16) | (byte_ptr[1] << 8) | byte_ptr[0];
            }
            // Sign extend
            if (sample & 0x800000) {
                sample |= 0xFF000000;
            }
            // Scale up to 32-bit
            sample = sample << 8;
        } else if (src_bit_depth == 32) {
            int32_t *src_samples = (int32_t *)src_data;
            sample = src_samples[i];
        }
        
        // Now store the sample in the destination format
        if (dst_bit_depth == 16) {
            int16_t *dst_samples = (int16_t *)dst_data;
            // Scale down from 32-bit to 16-bit
            dst_samples[i] = (int16_t)(sample >> 16);
        } else if (dst_bit_depth == 24) {
            // 24-bit is stored in 3 bytes
            uint8_t *byte_ptr = dst_data + (i * 3);
            // Scale down from 32-bit to 24-bit
            int32_t val_24bit = sample >> 8;
            if (cfg->is_big_endian) {
                byte_ptr[0] = (val_24bit >> 16) & 0xFF;
                byte_ptr[1] = (val_24bit >> 8) & 0xFF;
                byte_ptr[2] = val_24bit & 0xFF;
            } else {
                byte_ptr[0] = val_24bit & 0xFF;
                byte_ptr[1] = (val_24bit >> 8) & 0xFF; 
                byte_ptr[2] = (val_24bit >> 16) & 0xFF;
            }
        } else if (dst_bit_depth == 32) {
            int32_t *dst_samples = (int32_t *)dst_data;
            dst_samples[i] = sample;
        }
    }
    
    out_buffer->data_size = dst_size;
    out_buffer->timestamp = in_buffer->timestamp;
    
    return ESP_OK;
}

// Helper function to swap endianness for 16-bit samples
static esp_err_t test_swap_endianness_16bit(audio_buffer_t *in_buffer,
                                           audio_buffer_t *out_buffer)
{
    if (!in_buffer || !out_buffer) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (in_buffer->data_size % 2 != 0) {
        return ESP_ERR_INVALID_SIZE;  // Must be multiple of 2 bytes
    }
    
    if (in_buffer->data_size > out_buffer->size) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    uint16_t *src = (uint16_t *)in_buffer->data;
    uint16_t *dst = (uint16_t *)out_buffer->data;
    int sample_count = in_buffer->data_size / 2;
    
    for (int i = 0; i < sample_count; i++) {
        // Swap bytes: ((value & 0xFF) << 8) | ((value >> 8) & 0xFF)
        dst[i] = ((src[i] & 0xFF) << 8) | ((src[i] >> 8) & 0xFF);
    }
    
    out_buffer->data_size = in_buffer->data_size;
    out_buffer->timestamp = in_buffer->timestamp;
    
    return ESP_OK;
}

/**
 * @brief Test 16-bit PCM format handling
 */
void test_pcm_16bit_format(void)
{
    ESP_LOGI(TAG, "Testing 16-bit PCM format handling");
    
    // Create audio buffer configuration
    audio_buffer_cfg_t config = {
        .buffer_size = 1024,
        .buffer_count = 4,
        .sample_rate = 44100,
        .bits_per_sample = 16,
        .num_channels = 2
    };
    
    // Initialize buffer pool
    audio_buffer_pool_t *pool = audio_buffer_pool_init(&config);
    TEST_ASSERT_NOT_NULL(pool);
    
    // Get a buffer to work with
    audio_buffer_t *buffer = audio_buffer_get(pool);
    TEST_ASSERT_NOT_NULL(buffer);
    
    // Fill buffer with test data - a sine wave at 440Hz
    int16_t *samples = (int16_t *)buffer->data;
    int num_samples = config.buffer_size / 2; // 16-bit = 2 bytes per sample
    
    for (int i = 0; i < num_samples; i++) {
        double time = (double)i / config.sample_rate;
        double sine_val = sin(2 * M_PI * 440 * time);
        samples[i] = (int16_t)(sine_val * 32767.0); // Max amplitude for 16-bit
    }
    buffer->data_size = config.buffer_size;
    
    // Verify data pattern - check first few samples
    TEST_ASSERT_NOT_EQUAL(0, samples[0]);
    TEST_ASSERT_NOT_EQUAL(0, samples[1]);
    
    // Test endianness swapping
    audio_buffer_t *swapped_buffer = audio_buffer_get(pool);
    TEST_ASSERT_NOT_NULL(swapped_buffer);
    
    TEST_ASSERT_EQUAL(ESP_OK, test_swap_endianness_16bit(buffer, swapped_buffer));
    
    // Verify that data is correctly swapped
    uint16_t *orig_samples = (uint16_t *)buffer->data;
    uint16_t *swapped_samples = (uint16_t *)swapped_buffer->data;
    
    for (int i = 0; i < 10; i++) {
        uint16_t expected = ((orig_samples[i] & 0xFF) << 8) | ((orig_samples[i] >> 8) & 0xFF);
        TEST_ASSERT_EQUAL(expected, swapped_samples[i]);
    }
    
    // Clean up
    audio_buffer_release(pool, buffer);
    audio_buffer_release(pool, swapped_buffer);
    audio_buffer_pool_deinit(pool);
}

/**
 * @brief Test 24-bit PCM format handling
 */
void test_pcm_24bit_format(void)
{
    ESP_LOGI(TAG, "Testing 24-bit PCM format handling");
    
    // Create audio buffer configuration for 24-bit samples
    audio_buffer_cfg_t config = {
        .buffer_size = 1536,  // For 512 24-bit samples (512 * 3 = 1536)
        .buffer_count = 4,
        .sample_rate = 48000,
        .bits_per_sample = 24,
        .num_channels = 2
    };
    
    // Initialize buffer pool
    audio_buffer_pool_t *pool = audio_buffer_pool_init(&config);
    TEST_ASSERT_NOT_NULL(pool);
    
    // Get a buffer for 24-bit samples
    audio_buffer_t *buffer_24bit = audio_buffer_get(pool);
    TEST_ASSERT_NOT_NULL(buffer_24bit);
    
    // Fill buffer with 24-bit test data (3 bytes per sample)
    uint8_t *data = (uint8_t *)buffer_24bit->data;
    int num_samples = config.buffer_size / 3; // 24-bit = 3 bytes per sample
    
    // Create a pattern where each sample has a unique value
    for (int i = 0; i < num_samples; i++) {
        // Generate sample value (range -8,388,608 to 8,388,607)
        int32_t sample_val = i * 1000;
        if (sample_val > 8388607) {
            sample_val = 8388607;  // Max positive 24-bit value
        } else if (i % 2) {
            sample_val = -sample_val;  // Make some samples negative
        }
        
        // Store in little-endian format (LSB first)
        data[i*3]     = sample_val & 0xFF;
        data[i*3 + 1] = (sample_val >> 8) & 0xFF;
        data[i*3 + 2] = (sample_val >> 16) & 0xFF;
    }
    buffer_24bit->data_size = config.buffer_size;
    
    // Test conversion from 24-bit to 16-bit
    audio_buffer_cfg_t config_16bit = {
        .buffer_size = 1024,  // For 512 16-bit samples (512 * 2 = 1024)
        .buffer_count = 4,
        .sample_rate = 48000,
        .bits_per_sample = 16,
        .num_channels = 2
    };
    
    audio_buffer_pool_t *pool_16bit = audio_buffer_pool_init(&config_16bit);
    TEST_ASSERT_NOT_NULL(pool_16bit);
    
    audio_buffer_t *buffer_16bit = audio_buffer_get(pool_16bit);
    TEST_ASSERT_NOT_NULL(buffer_16bit);
    
    // Set up conversion configuration
    pcm_convert_cfg_t convert_cfg = {
        .src_bit_depth = 24,
        .dst_bit_depth = 16,
        .is_big_endian = false
    };
    
    // Perform the conversion
    TEST_ASSERT_EQUAL(ESP_OK, test_pcm_bit_depth_convert(buffer_24bit, buffer_16bit, &convert_cfg));
    
    // Verify conversion - check that the buffer size is correct
    TEST_ASSERT_EQUAL(num_samples * 2, buffer_16bit->data_size);
    
    // Clean up
    audio_buffer_release(pool, buffer_24bit);
    audio_buffer_release(pool_16bit, buffer_16bit);
    audio_buffer_pool_deinit(pool);
    audio_buffer_pool_deinit(pool_16bit);
}

/**
 * @brief Test PCM endianness handling
 */
void test_pcm_endianness(void)
{
    ESP_LOGI(TAG, "Testing PCM endianness handling");
    
    // Create audio buffer configuration
    audio_buffer_cfg_t config = {
        .buffer_size = 1024,
        .buffer_count = 4,
        .sample_rate = 44100,
        .bits_per_sample = 16,
        .num_channels = 2
    };
    
    // Initialize buffer pool
    audio_buffer_pool_t *pool = audio_buffer_pool_init(&config);
    TEST_ASSERT_NOT_NULL(pool);
    
    // Get a buffer to work with
    audio_buffer_t *le_buffer = audio_buffer_get(pool);
    TEST_ASSERT_NOT_NULL(le_buffer);
    
    // Get a buffer for big-endian data
    audio_buffer_t *be_buffer = audio_buffer_get(pool);
    TEST_ASSERT_NOT_NULL(be_buffer);
    
    // Fill buffer with test data in little-endian format
    uint16_t test_values[] = {0x1234, 0x5678, 0x9ABC, 0xDEF0, 0x0246};
    int16_t *le_samples = (int16_t *)le_buffer->data;
    
    for (int i = 0; i < 5; i++) {
        le_samples[i] = test_values[i];
    }
    le_buffer->data_size = 5 * sizeof(uint16_t);
    
    // Convert to big-endian
    TEST_ASSERT_EQUAL(ESP_OK, test_swap_endianness_16bit(le_buffer, be_buffer));
    
    // Verify big-endian values
    uint16_t *be_samples = (uint16_t *)be_buffer->data;
    
    for (int i = 0; i < 5; i++) {
        uint16_t expected_be = ((test_values[i] & 0xFF) << 8) | ((test_values[i] >> 8) & 0xFF);
        TEST_ASSERT_EQUAL_HEX16(expected_be, be_samples[i]);
        
        // Double check by manually comparing bytes
        uint8_t *le_bytes = (uint8_t *)&le_samples[i];
        uint8_t *be_bytes = (uint8_t *)&be_samples[i];
        
        TEST_ASSERT_EQUAL(le_bytes[0], be_bytes[1]);
        TEST_ASSERT_EQUAL(le_bytes[1], be_bytes[0]);
    }
    
    // Clean up
    audio_buffer_release(pool, le_buffer);
    audio_buffer_release(pool, be_buffer);
    audio_buffer_pool_deinit(pool);
}

/**
 * @brief Test 16-bit to 32-bit conversion
 */
void test_pcm_16bit_to_32bit(void)
{
    ESP_LOGI(TAG, "Testing 16-bit to 32-bit PCM conversion");
    
    // Create audio buffer configurations
    audio_buffer_cfg_t config_16bit = {
        .buffer_size = 1024,  // 512 16-bit samples
        .buffer_count = 4,
        .sample_rate = 48000,
        .bits_per_sample = 16,
        .num_channels = 2
    };
    
    audio_buffer_cfg_t config_32bit = {
        .buffer_size = 2048,  // 512 32-bit samples
        .buffer_count = 4,
        .sample_rate = 48000,
        .bits_per_sample = 32,
        .num_channels = 2
    };
    
    // Initialize buffer pools
    audio_buffer_pool_t *pool_16bit = audio_buffer_pool_init(&config_16bit);
    TEST_ASSERT_NOT_NULL(pool_16bit);
    
    audio_buffer_pool_t *pool_32bit = audio_buffer_pool_init(&config_32bit);
    TEST_ASSERT_NOT_NULL(pool_32bit);
    
    // Get buffers
    audio_buffer_t *buffer_16bit = audio_buffer_get(pool_16bit);
    TEST_ASSERT_NOT_NULL(buffer_16bit);
    
    audio_buffer_t *buffer_32bit = audio_buffer_get(pool_32bit);
    TEST_ASSERT_NOT_NULL(buffer_32bit);
    
    // Fill 16-bit buffer with test pattern
    int16_t *samples_16bit = (int16_t *)buffer_16bit->data;
    for (int i = 0; i < 512; i++) {
        samples_16bit[i] = (i % 100) * 328; // Values between 0 and 32767
    }
    buffer_16bit->data_size = 1024;
    
    // Set up conversion configuration
    pcm_convert_cfg_t convert_cfg = {
        .src_bit_depth = 16,
        .dst_bit_depth = 32,
        .is_big_endian = false
    };
    
    // Perform conversion
    TEST_ASSERT_EQUAL(ESP_OK, test_pcm_bit_depth_convert(buffer_16bit, buffer_32bit, &convert_cfg));
    
    // Verify conversion
    int32_t *samples_32bit = (int32_t *)buffer_32bit->data;
    for (int i = 0; i < 10; i++) {
        // 16-bit value should be scaled up to 32-bit by left-shifting 16 bits
        int32_t expected = ((int32_t)samples_16bit[i]) << 16;
        TEST_ASSERT_EQUAL(expected, samples_32bit[i]);
    }
    
    // Clean up
    audio_buffer_release(pool_16bit, buffer_16bit);
    audio_buffer_release(pool_32bit, buffer_32bit);
    audio_buffer_pool_deinit(pool_16bit);
    audio_buffer_pool_deinit(pool_32bit);
}

void run_pcm_format_tests(void)
{
    ESP_LOGI(TAG, "Starting PCM format validation tests");
    
    UNITY_BEGIN();
    
    // Run all PCM format tests
    RUN_TEST(test_pcm_16bit_format);
    RUN_TEST(test_pcm_24bit_format);
    RUN_TEST(test_pcm_endianness);
    RUN_TEST(test_pcm_16bit_to_32bit);
    
    int failures = UNITY_END();
    
    ESP_LOGI(TAG, "PCM format tests completed with %d failures", failures);
}
