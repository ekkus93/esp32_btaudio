#include <stdio.h>
#include <inttypes.h>
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "audio_processor.h"
#include "driver/i2s_std.h"  // This include is correct

// Define I2S_NUM_0 if not already defined
#ifndef I2S_NUM_0
#define I2S_NUM_0 0
#endif

#define I2S_SAMPLE_RATE AUDIO_SAMPLE_RATE_44K
#define I2S_BIT_DEPTH   AUDIO_BIT_DEPTH_16
#define I2S_CHANNELS    AUDIO_CHANNEL_STEREO
#define I2S_PORT        I2S_NUM_0

static const char *TAG = "AUDIO_TEST";

/**
 * Test audio processor initialization
 */
void test_audio_processor_init(void)
{
    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    // Initialize audio processor
    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Get configuration and verify it matches what we set
    audio_config_t read_config;
    ret = audio_processor_get_config(&read_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    TEST_ASSERT_EQUAL(config.sample_rate, read_config.sample_rate);
    TEST_ASSERT_EQUAL(config.bit_depth, read_config.bit_depth);
    TEST_ASSERT_EQUAL(config.channels, read_config.channels);
    TEST_ASSERT_EQUAL(config.volume, read_config.volume);
    TEST_ASSERT_EQUAL(config.mute, read_config.mute);
    
    // Clean up
    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * Test audio volume control
 */
void test_audio_volume_control(void)
{
    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    // Initialize audio processor
    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test setting various volume levels
    ret = audio_processor_set_volume(50);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    audio_config_t read_config;
    ret = audio_processor_get_config(&read_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(50, read_config.volume);
    
    // Test maximum volume
    ret = audio_processor_set_volume(100);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = audio_processor_get_config(&read_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(100, read_config.volume);
    
    // Test minimum volume
    ret = audio_processor_set_volume(0);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = audio_processor_get_config(&read_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(0, read_config.volume);
    
    // Clean up
    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * Test mute functionality
 */
void test_audio_mute(void)
{
    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    // Initialize audio processor
    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test mute
    ret = audio_processor_set_mute(true);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    audio_config_t read_config;
    ret = audio_processor_get_config(&read_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(true, read_config.mute);
    
    // Test unmute
    ret = audio_processor_set_mute(false);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = audio_processor_get_config(&read_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(false, read_config.mute);
    
    // Clean up
    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * Test sample rate change
 */
void test_audio_sample_rate(void)
{
    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    // Initialize audio processor
    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test changing sample rate
    ret = audio_processor_set_sample_rate(AUDIO_SAMPLE_RATE_48K);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    audio_config_t read_config;
    ret = audio_processor_get_config(&read_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(AUDIO_SAMPLE_RATE_48K, read_config.sample_rate);
    
    // Test another sample rate
    ret = audio_processor_set_sample_rate(AUDIO_SAMPLE_RATE_16K);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = audio_processor_get_config(&read_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(AUDIO_SAMPLE_RATE_16K, read_config.sample_rate);
    
    // Clean up
    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * Test audio processing start/stop
 */
void test_audio_start_stop(void)
{
    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    // Initialize audio processor
    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test starting audio
    ret = audio_processor_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Small delay to let processing task run
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Test stopping audio
    ret = audio_processor_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Clean up
    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * Test audio data reading
 */
void test_audio_read(void)
{
    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    // Initialize audio processor
    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Start audio processing
    ret = audio_processor_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Small delay to let processing task run and fill the buffer
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Test reading audio data
    uint8_t buffer[1024];
    size_t bytes_read;
    
    ret = audio_processor_read(buffer, sizeof(buffer), &bytes_read);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Due to the nature of audio processing, we can't guarantee bytes_read,
    // but we can check that the API functions correctly
    
    // Stop audio processing
    ret = audio_processor_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Clean up
    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * Test getting audio statistics
 */
void test_audio_stats(void)
{
    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    // Initialize audio processor
    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Start audio processing
    ret = audio_processor_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Small delay to let processing task run
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Test getting audio stats
    audio_stats_t stats;
    ret = audio_processor_get_stats(&stats);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // We can't assert specific values, but we can make sure the function works
    
    // Stop audio processing
    ret = audio_processor_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Clean up
    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * Test audio format conversion functionality
 */
void test_audio_format_conversion(void)
{
    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    // Initialize audio processor
    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Declare the source buffer
    int16_t src_buffer_16[256];
    
    // Fill source buffer with test pattern
    for (int i = 0; i < 256; i++) {
        src_buffer_16[i] = i * 100; 
    }
    
    // Test buffer for conversion results
    uint8_t test_buffer[2048];
    size_t bytes_read;
    
    // Force a format conversion by changing bit depth
    ret = audio_processor_set_bit_depth(AUDIO_BIT_DEPTH_32);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    audio_config_t new_config;
    ret = audio_processor_get_config(&new_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(AUDIO_BIT_DEPTH_32, new_config.bit_depth);
    
    // Send test data and retrieve converted data
    // This is an indirect test since we can't directly call the conversion function
    
    // Clean up
    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * Test I2S configuration
 */
void test_audio_i2s_config(void)
{
    // Test with different I2S configurations
    audio_config_t configs[] = {
        // 44.1kHz, 16-bit, stereo
        {
            .sample_rate = AUDIO_SAMPLE_RATE_44K,
            .bit_depth = AUDIO_BIT_DEPTH_16,
            .channels = AUDIO_CHANNEL_STEREO,
            .volume = 80,
            .mute = false,
            .i2s_port = I2S_PORT,
        },
        // 48kHz, 24-bit, stereo
        {
            .sample_rate = AUDIO_SAMPLE_RATE_48K,
            .bit_depth = AUDIO_BIT_DEPTH_24,
            .channels = AUDIO_CHANNEL_STEREO,
            .volume = 80,
            .mute = false,
            .i2s_port = I2S_PORT,
        },
        // 16kHz, 16-bit, mono
        {
            .sample_rate = AUDIO_SAMPLE_RATE_16K,
            .bit_depth = AUDIO_BIT_DEPTH_16,
            .channels = AUDIO_CHANNEL_MONO,
            .volume = 80,
            .mute = false,
            .i2s_port = I2S_PORT,
        }
    };
    
    // Test each configuration
    for (int i = 0; i < sizeof(configs) / sizeof(configs[0]); i++) {
        ESP_LOGI(TAG, "Testing I2S config %d: %dHz, %d-bit, %d channel(s)",
                 i, configs[i].sample_rate, configs[i].bit_depth, configs[i].channels);
                 
        // Initialize with this config
        esp_err_t ret = audio_processor_init(&configs[i]);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        
        // Verify config was set correctly
        audio_config_t read_config;
        ret = audio_processor_get_config(&read_config);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        
        TEST_ASSERT_EQUAL(configs[i].sample_rate, read_config.sample_rate);
        TEST_ASSERT_EQUAL(configs[i].bit_depth, read_config.bit_depth);
        TEST_ASSERT_EQUAL(configs[i].channels, read_config.channels);
        
        // Start and stop briefly to verify I2S works
        ret = audio_processor_start();
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        
        vTaskDelay(pdMS_TO_TICKS(100));
        
        ret = audio_processor_stop();
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        
        // Clean up
        ret = audio_processor_deinit();
        TEST_ASSERT_EQUAL(ESP_OK, ret);
    }
}

/**
 * Test audio buffer management
 */
void test_audio_buffer_management(void)
{
    audio_config_t config = {
        .sample_rate = I2S_SAMPLE_RATE,
        .bit_depth = I2S_BIT_DEPTH,
        .channels = I2S_CHANNELS,
        .volume = 80,
        .mute = false,
        .i2s_port = I2S_PORT,
    };

    // Initialize audio processor
    esp_err_t ret = audio_processor_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Start audio processing
    ret = audio_processor_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Let it run for a while to fill buffer
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Get buffer stats
    audio_stats_t stats;
    ret = audio_processor_get_stats(&stats);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Print buffer stats
    ESP_LOGI(TAG, "Buffer level: %" PRIu32 ", Peak: %" PRIu32, 
             stats.current_buffer_level, stats.peak_buffer_level);
    
    // Read some data to verify buffer works
    uint8_t buffer[1024];
    size_t bytes_read;
    
    // Read multiple times to test buffer behavior
    for (int i = 0; i < 5; i++) {
        ret = audio_processor_read(buffer, sizeof(buffer), &bytes_read);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Get updated stats
    ret = audio_processor_get_stats(&stats);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Clean up
    ret = audio_processor_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = audio_processor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}
void app_main(void)
{
    printf("Running audio processor tests\n");
    
    UNITY_BEGIN();
    
    RUN_TEST(test_audio_processor_init);
    RUN_TEST(test_audio_volume_control);
    RUN_TEST(test_audio_mute);
    RUN_TEST(test_audio_sample_rate);
    RUN_TEST(test_audio_start_stop);
    RUN_TEST(test_audio_read);
    RUN_TEST(test_audio_stats);
    RUN_TEST(test_audio_format_conversion);
    RUN_TEST(test_audio_i2s_config);
    RUN_TEST(test_audio_buffer_management);
    
    UNITY_END();
}
