// Moved from top-level test/component/test_audio_processor.c
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "unity.h"
#include "audio_processor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Mock for I2S functions
#include "mock_i2s_std.h"

/* Test-only hooks from audio_processor.c (enabled under CONFIG_BT_MOCK_TESTING) */
extern bool audio_source_tag_test_init_buffer(size_t buf_size);
extern void audio_source_tag_test_reset_buffer(void);
extern bool audio_source_tag_test_push(int tag);
extern size_t audio_processor_test_get_tag_used(void);
extern uint32_t audio_processor_test_get_tag_miss_count(void);
extern void audio_processor_test_reset_tag_miss_count(void);
#ifdef CONFIG_BT_MOCK_TESTING
extern size_t audio_processor_test_get_beep_remaining_bytes(void);
#endif

void setUp(void)
{
    // Setup runs before each test
}

void tearDown(void)
{
    // Clean up after each test
    audio_processor_deinit();
}

// Test basic initialization
void test_audio_processor_init(void)
{
    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 80
    };
    
    // Set up mock expectations
    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    
    // Test the function
    esp_err_t ret = audio_processor_init(&config);
    
    // Verify results
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

// Test setting volume
void test_audio_processor_set_volume(void)
{
    // Initialize first
    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 50
    };
    
    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    audio_processor_init(&config);
    
    // Test valid volume
    esp_err_t ret = audio_processor_set_volume(75);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test that volume is actually set
    audio_config_t current_config;
    audio_processor_get_config(&current_config);
    TEST_ASSERT_EQUAL(75, current_config.volume);
    
    // Test volume clamping
    ret = audio_processor_set_volume(150);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    audio_processor_get_config(&current_config);
    TEST_ASSERT_EQUAL(100, current_config.volume);
}

// Test volume application to audio data
void test_audio_processor_volume_application(void)
{
    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 100  // Full volume
    };
    
    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    audio_processor_init(&config);
    
    // Start processing
    esp_err_t ret = audio_processor_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Create test audio data (16-bit stereo samples)
    const int num_samples = 128;  // 64 frames of stereo
    const size_t data_size = num_samples * sizeof(int16_t);
    int16_t test_data[num_samples];
    
    // Fill with known values (alternating positive/negative for testing)
    for (int i = 0; i < num_samples; i++) {
        test_data[i] = (i % 2 == 0) ? 10000 : -5000;
    }
    
    // Inject test data into the ring buffer
    ret = audio_processor_test_inject_audio_data((const uint8_t*)test_data, data_size);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Read audio data at full volume (100%)
    uint8_t buffer1[1024];
    size_t bytes_read1;
    ret = audio_processor_read(buffer1, sizeof(buffer1), &bytes_read1);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(data_size, bytes_read1);
    
    // Verify the data matches what we injected (no volume scaling at 100%)
    int16_t* samples1 = (int16_t*)buffer1;
    for (int i = 0; i < num_samples; i++) {
        TEST_ASSERT_EQUAL_INT16(test_data[i], samples1[i]);
    }
    
    // Inject the same data again
    ret = audio_processor_test_inject_audio_data((const uint8_t*)test_data, data_size);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Set volume to 50%
    ret = audio_processor_set_volume(50);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Read audio data at reduced volume
    uint8_t buffer2[1024];
    size_t bytes_read2;
    ret = audio_processor_read(buffer2, sizeof(buffer2), &bytes_read2);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(data_size, bytes_read2);
    
    // Verify volume scaling was applied (samples should be halved)
    int16_t* samples2 = (int16_t*)buffer2;
    for (int i = 0; i < num_samples; i++) {
        int16_t expected = (int16_t)(test_data[i] * 0.5f);
        TEST_ASSERT_EQUAL_INT16(expected, samples2[i]);
    }
    
    // Test volume = 0 (muted)
    ret = audio_processor_test_inject_audio_data((const uint8_t*)test_data, data_size);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = audio_processor_set_volume(0);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    uint8_t buffer3[1024];
    size_t bytes_read3;
    ret = audio_processor_read(buffer3, sizeof(buffer3), &bytes_read3);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(data_size, bytes_read3);
    
    // Verify all samples are zero (muted)
    int16_t* samples3 = (int16_t*)buffer3;
    for (int i = 0; i < num_samples; i++) {
        TEST_ASSERT_EQUAL_INT16(0, samples3[i]);
    }
}

// Test read buffer filling
void test_audio_processor_read_buffer_fill(void)
{
    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 100
    };
    
    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    audio_processor_init(&config);
    
    esp_err_t ret = audio_processor_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Create test data
    const size_t test_data_size = 512;
    uint8_t test_data[test_data_size];
    for (size_t i = 0; i < test_data_size; i++) {
        test_data[i] = (uint8_t)i;
    }
    
    // Inject test data
    ret = audio_processor_test_inject_audio_data(test_data, test_data_size);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test reading different buffer sizes
    uint8_t small_buffer[64];
    size_t bytes_read_small;
    ret = audio_processor_read(small_buffer, sizeof(small_buffer), &bytes_read_small);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(sizeof(small_buffer), bytes_read_small);
    
    // Verify data matches
    for (size_t i = 0; i < bytes_read_small; i++) {
        TEST_ASSERT_EQUAL(test_data[i], small_buffer[i]);
    }
    
    // Inject more data
    ret = audio_processor_test_inject_audio_data(test_data, test_data_size);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test reading larger buffer
    uint8_t large_buffer[1024];
    size_t bytes_read_large;
    ret = audio_processor_read(large_buffer, sizeof(large_buffer), &bytes_read_large);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(960, bytes_read_large);  // Should read all available data (448 remaining + 512 new)
    
    // Verify data matches (first 448 bytes should be from offset 64 of original data)
    for (size_t i = 0; i < 448; i++) {
        TEST_ASSERT_EQUAL(test_data[i + 64], large_buffer[i]);
    }
    // Next 512 bytes should be the new injection
    for (size_t i = 0; i < 512; i++) {
        TEST_ASSERT_EQUAL(test_data[i], large_buffer[i + 448]);
    }
    
    // Test reading from empty buffer
    uint8_t empty_buffer[64];
    size_t bytes_read_empty;
    ret = audio_processor_read(empty_buffer, sizeof(empty_buffer), &bytes_read_empty);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(0, bytes_read_empty);  // No data available
}

/* Ensure beep enqueues do not proceed when metadata tags cannot be pushed. */
void test_audio_processor_beep_aborts_when_tag_buffer_full(void)
{
    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 80
    };

    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    /* Fill the tag buffer until pushes fail to force a tag-push failure. */
    int pushes = 0;
    while (audio_source_tag_test_push(0xAB)) {
        pushes++;
        if (pushes > 4096) break; /* defensive bound */
    }

    /* Beep should refuse to enqueue if it cannot push the tag. */
    esp_err_t ret = audio_processor_beep_tone(100, 440.0);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);

    /* And no audio should be readable. */
    uint8_t buf[512] = {0};
    size_t bytes_read = 0;
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)bytes_read);

    audio_source_tag_test_reset_buffer();
}

/* Ensure beep enqueues tags alongside audio under normal conditions. */
void test_audio_processor_beep_pushes_tags(void)
{
    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 80
    };

    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    size_t tags_before = audio_processor_test_get_tag_used();

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep_tone(100, 440.0));

    size_t tags_after = audio_processor_test_get_tag_used();
    TEST_ASSERT_TRUE(tags_after > tags_before);

    audio_source_tag_test_reset_buffer();
}

void test_audio_processor_beep_busy_when_i2s_active(void)
{
    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 80
    };

    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    /* Disable synth keepalive to model live I2S capture being active. */
    audio_processor_set_synth_mode(false);
    esp_err_t ret = audio_processor_beep_tone(100, 440.0);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);

    audio_processor_stop();
    audio_processor_deinit();
}

void test_audio_processor_play_busy_when_i2s_active(void)
{
    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 80
    };

    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    /* Model live I2S capture (keepalive disabled) and ensure PLAY rejects with BUSY. */
    audio_processor_set_synth_mode(false);
    esp_err_t ret = audio_processor_play_wav("/spiffs/does_not_exist.wav");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);

    audio_processor_stop();
    audio_processor_deinit();
}

void test_audio_processor_beep_busy_when_wav_active(void)
{
    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 80
    };

    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    audio_processor_test_wav_begin();
    esp_err_t ret = audio_processor_beep_tone(50, 440.0);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    audio_processor_test_wav_abort();

    audio_processor_stop();
    audio_processor_deinit();
}

void test_audio_processor_start_preempts_beep_and_wav(void)
{
    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 80
    };

    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));

    /* Seed active WAV + beep before starting I2S to ensure they are preempted. */
    audio_processor_test_wav_reset_state();
    audio_processor_test_wav_begin();
    audio_processor_test_wav_add_pending(1024);
    TEST_ASSERT_TRUE(audio_processor_test_wav_is_active());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep_tone(100, 440.0));
    TEST_ASSERT_GREATER_THAN_UINT32(0, audio_processor_test_get_beep_remaining_bytes());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    TEST_ASSERT_FALSE(audio_processor_test_wav_is_active());
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_test_get_beep_remaining_bytes());
    TEST_ASSERT_EQUAL_size_t(0, audio_processor_test_get_tag_used());

    audio_processor_stop();
    audio_processor_deinit();
}

void test_audio_processor_beep_disables_synth_keepalive(void)
{
    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 80
    };

    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    audio_processor_set_synth_mode(true);
    TEST_ASSERT_TRUE(audio_processor_is_synth_mode_enabled());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep_tone(50, 880.0));
    TEST_ASSERT_FALSE(audio_processor_is_synth_mode_enabled());

    audio_processor_stop();
    audio_processor_deinit();
}

void test_audio_processor_play_disables_synth_keepalive(void)
{
    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 80
    };

    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    audio_processor_set_synth_mode(true);
    TEST_ASSERT_TRUE(audio_processor_is_synth_mode_enabled());

    /* PLAY should drop synth even if file open fails. */
    (void)audio_processor_play_wav("/spiffs/missing.wav");
    TEST_ASSERT_FALSE(audio_processor_is_synth_mode_enabled());

    audio_processor_stop();
    audio_processor_deinit();
}

void test_audio_processor_play_busy_when_beep_active(void)
{
    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 80
    };

    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    /* With synth keepalive active, enqueue a beep then ensure PLAY is busy. */
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep_tone(100, 440.0));
    esp_err_t ret = audio_processor_play_wav("/spiffs/dummy.wav");
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);

    audio_processor_stop();
    audio_processor_deinit();
}

/* Ensure beeps do not produce TAG-MISS when enqueued and drained. */
void test_audio_processor_beep_no_tag_miss(void)
{
    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 80
    };

    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    audio_source_tag_test_reset_buffer();
    audio_processor_test_reset_tag_miss_count();

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep_tone(120, 440.0));

    uint8_t buf[1024];
    size_t bytes_read = 0;
    int loops = 0;
    do {
        TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(buf, sizeof(buf), &bytes_read));
        loops++;
        if (loops > 64) break; /* safety bound */
    } while (bytes_read > 0);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, audio_processor_test_get_tag_miss_count(), "TAG-MISS occurred while draining beep audio");

    audio_source_tag_test_reset_buffer();
    audio_processor_test_reset_tag_miss_count();
}

/* Injected audio should carry a tag so consumer reads do not trigger TAG-MISS. */
void test_audio_processor_inject_pushes_and_consumes_tag(void)
{
    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 80
    };

    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    audio_source_tag_test_reset_buffer();

    const size_t data_size = 256;
    uint8_t data[data_size];
    for (size_t i = 0; i < data_size; ++i) {
        data[i] = (uint8_t)(i & 0xFF);
    }

    size_t tags_before = audio_processor_test_get_tag_used();
    TEST_ASSERT_EQUAL_size_t(0, tags_before);

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_test_inject_audio_data(data, data_size));

    size_t tags_after = audio_processor_test_get_tag_used();
    TEST_ASSERT_TRUE(tags_after > tags_before);

    uint8_t out[data_size];
    size_t bytes_read = 0;
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(out, sizeof(out), &bytes_read));
    TEST_ASSERT_EQUAL_size_t(data_size, bytes_read);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out, data_size);

    size_t tags_final = audio_processor_test_get_tag_used();
    TEST_ASSERT_EQUAL_size_t(0, tags_final);
}

// Test that a beep enqueued while muted still produces audible data in the
// ring buffer and that the beep-active flag is set. This verifies the
// beep-bypass-mute behavior implemented by audio_processor_beep().
void test_audio_processor_beep_bypasses_mute(void)
{
    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 80
    };

    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    // Mute the output and request a short beep
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_set_mute(true));
    TEST_ASSERT_FALSE(audio_processor_is_beep_active());

    // Enqueue a middle-C tone for 200ms (shortened for test runtime)
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep_tone(200, 261.63));
    TEST_ASSERT_TRUE(audio_processor_is_beep_active());

    // Read available audio - should return beep data even though muted
    uint8_t outbuf[1024];
    size_t bytes_read = 0;
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(outbuf, sizeof(outbuf), &bytes_read));
    TEST_ASSERT_GREATER_THAN(0, (int)bytes_read);

    // Ensure the returned data is not all zeros (i.e., not silence)
    bool any_nonzero = false;
    for (size_t i = 0; i < bytes_read; ++i) {
        if (outbuf[i] != 0) { any_nonzero = true; break; }
    }
    TEST_ASSERT_TRUE(any_nonzero);
}

#ifdef CONFIG_BT_MOCK_TESTING
/* Beep prefill should release after the configured delay and allow data to drain. */
void test_audio_processor_beep_prefill_releases_after_delay(void)
{
    audio_config_t config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 80
    };

    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep_tone(200, 440.0));
    TEST_ASSERT_TRUE(audio_processor_is_beep_active());

    size_t before = audio_processor_test_get_beep_remaining_bytes();
    TEST_ASSERT_TRUE(before > 0);

    vTaskDelay(pdMS_TO_TICKS(450));

    uint8_t outbuf[1024];
    size_t bytes_read = 0;
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_read(outbuf, sizeof(outbuf), &bytes_read));
    TEST_ASSERT_GREATER_THAN_UINT32(0, bytes_read);

    size_t after = audio_processor_test_get_beep_remaining_bytes();
    TEST_ASSERT_TRUE_MESSAGE(after < before, "beep remaining bytes should drop after prefill release");
}
#endif

#ifdef CONFIG_BT_MOCK_TESTING
void test_audio_processor_wav_begin_tracks_state(void)
{
    audio_processor_test_wav_reset_state();
    audio_processor_set_synth_mode(true);
    TEST_ASSERT_TRUE(audio_processor_is_synth_mode_enabled());

    audio_processor_test_wav_begin();

    TEST_ASSERT_TRUE(audio_processor_test_wav_is_active());
    TEST_ASSERT_FALSE(audio_processor_is_synth_mode_enabled());
    TEST_ASSERT_EQUAL_size_t(0, audio_processor_test_wav_pending_bytes());

    audio_processor_test_wav_add_pending(512);
    TEST_ASSERT_TRUE(audio_processor_test_wav_is_active());
    TEST_ASSERT_EQUAL_size_t(512, audio_processor_test_wav_pending_bytes());

    audio_processor_test_wav_add_pending(SIZE_MAX);
    TEST_ASSERT_EQUAL_size_t(SIZE_MAX, audio_processor_test_wav_pending_bytes());
}

void test_audio_processor_wav_consume_requires_completion_signal(void)
{
    audio_processor_test_wav_reset_state();
    audio_processor_set_synth_mode(true);
    audio_processor_test_wav_begin();
    audio_processor_test_wav_add_pending(1000);

    bool drained = audio_processor_test_wav_consume(600);
    TEST_ASSERT_FALSE(drained);
    TEST_ASSERT_TRUE(audio_processor_test_wav_is_active());
    TEST_ASSERT_EQUAL_size_t(400, audio_processor_test_wav_pending_bytes());
    TEST_ASSERT_FALSE(audio_processor_is_synth_mode_enabled());

    drained = audio_processor_test_wav_consume(500);
    TEST_ASSERT_TRUE(drained);
    TEST_ASSERT_TRUE(audio_processor_test_wav_is_active());
    TEST_ASSERT_EQUAL_size_t(0, audio_processor_test_wav_pending_bytes());
    TEST_ASSERT_FALSE(audio_processor_is_synth_mode_enabled());

    audio_processor_test_wav_complete_if_idle();
    TEST_ASSERT_FALSE(audio_processor_test_wav_is_active());
    TEST_ASSERT_TRUE(audio_processor_is_synth_mode_enabled());
}

void test_audio_processor_wav_complete_if_idle_requires_zero_pending(void)
{
    audio_processor_test_wav_reset_state();
    audio_processor_set_synth_mode(true);
    audio_processor_test_wav_begin();
    audio_processor_test_wav_add_pending(128);

    audio_processor_test_wav_complete_if_idle();
    TEST_ASSERT_TRUE(audio_processor_test_wav_is_active());
    TEST_ASSERT_FALSE(audio_processor_is_synth_mode_enabled());

    (void)audio_processor_test_wav_consume(64);
    audio_processor_test_wav_complete_if_idle();
    TEST_ASSERT_TRUE(audio_processor_test_wav_is_active());
    TEST_ASSERT_FALSE(audio_processor_is_synth_mode_enabled());

    (void)audio_processor_test_wav_consume(64);
    audio_processor_test_wav_complete_if_idle();
    TEST_ASSERT_FALSE(audio_processor_test_wav_is_active());
    TEST_ASSERT_TRUE(audio_processor_is_synth_mode_enabled());
}

void test_audio_processor_wav_abort_clears_state(void)
{
    audio_processor_test_wav_reset_state();
    audio_processor_set_synth_mode(true);
    audio_processor_test_wav_begin();
    audio_processor_test_wav_add_pending(2048);

    audio_processor_test_wav_abort();

    TEST_ASSERT_FALSE(audio_processor_test_wav_is_active());
    TEST_ASSERT_EQUAL_size_t(0, audio_processor_test_wav_pending_bytes());
    TEST_ASSERT_TRUE(audio_processor_is_synth_mode_enabled());

    /* Abort again to ensure idempotence when already idle */
    audio_processor_test_wav_abort();
    TEST_ASSERT_FALSE(audio_processor_test_wav_is_active());
    TEST_ASSERT_EQUAL_size_t(0, audio_processor_test_wav_pending_bytes());
    TEST_ASSERT_TRUE(audio_processor_is_synth_mode_enabled());
}
#endif // CONFIG_BT_MOCK_TESTING

// Add more tests for other functions...

int app_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_audio_processor_init);
    RUN_TEST(test_audio_processor_set_volume);
    RUN_TEST(test_audio_processor_volume_application);
    RUN_TEST(test_audio_processor_read_buffer_fill);
    RUN_TEST(test_audio_processor_beep_bypasses_mute);
    RUN_TEST(test_audio_processor_inject_pushes_and_consumes_tag);
    RUN_TEST(test_audio_processor_beep_no_tag_miss);
    RUN_TEST(test_audio_processor_beep_busy_when_i2s_active);
    RUN_TEST(test_audio_processor_play_busy_when_i2s_active);
    RUN_TEST(test_audio_processor_beep_busy_when_wav_active);
    RUN_TEST(test_audio_processor_start_preempts_beep_and_wav);
    RUN_TEST(test_audio_processor_beep_disables_synth_keepalive);
    RUN_TEST(test_audio_processor_play_disables_synth_keepalive);
#ifdef CONFIG_BT_MOCK_TESTING
    RUN_TEST(test_audio_processor_beep_prefill_releases_after_delay);
    RUN_TEST(test_audio_processor_wav_begin_tracks_state);
    RUN_TEST(test_audio_processor_wav_consume_requires_completion_signal);
    RUN_TEST(test_audio_processor_wav_complete_if_idle_requires_zero_pending);
    RUN_TEST(test_audio_processor_wav_abort_clears_state);
#endif
    // Run other tests...
    return UNITY_END();
}
