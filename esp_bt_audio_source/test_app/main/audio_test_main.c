#include <stdio.h>
#include "esp_log.h"
#include "unity.h"
#include "i2s_test.h"

static const char *TAG = "AUDIO_TEST";

/**
 * @brief Run all audio processing tests
 * 
 * This function runs the I2S driver configuration tests for audio input.
 */
void run_all_audio_tests(void)
{
    ESP_LOGI(TAG, "Starting audio processing tests");
    
    // Run I2S configuration tests
    ESP_LOGI(TAG, "Testing I2S driver configuration for audio input");
    run_i2s_tests();
    
    ESP_LOGI(TAG, "Audio processing tests completed");
    ESP_LOGI(TAG, "Remaining audio features to implement:");
    printf("\n==================================\n");
    printf("Remaining Audio Processing Tasks:\n");
    printf("- Set up audio data buffers and processing pipeline\n");
    printf("- Implement volume control functionality\n");
    printf("- Add mute/unmute capability\n");
    printf("- Support for different sample rates\n");
    printf("- Handle audio format conversion if needed\n");
    printf("==================================\n\n");
}
