#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

#include "i2s_audio_test.h"

static const char *TAG = "I2S_AUDIO_TEST";

// These need to be global for Unity to find them
void setUp(void)
{
    // Setup for I2S tests
}

void tearDown(void)
{
    // Cleanup after I2S tests
}

// Test functions need to be declared separately to use with RUN_TEST
void test_i2s_driver_init(void)
{
    ESP_LOGI(TAG, "Testing I2S channel initialization");

    i2s_chan_handle_t rx_handle = NULL;
    
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 8,
        .dma_frame_num = 64,
        .auto_clear = true,
    };
    
    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(rx_handle);
    
    // Clean up
    if (rx_handle) {
        i2s_del_channel(rx_handle);
    }
}

void test_i2s_standard_mode(void)
{
    ESP_LOGI(TAG, "Testing I2S standard config: 44100Hz, 16-bit, stereo");
    
    i2s_chan_handle_t rx_handle = NULL;
    
    // Test parameters
    uint32_t sample_rate = 44100;
    i2s_data_bit_width_t bit_width = I2S_DATA_BIT_WIDTH_16BIT;
    i2s_slot_mode_t channel_fmt = I2S_SLOT_MODE_STEREO;
    int gpio_bclk = GPIO_NUM_26;
    int gpio_ws = GPIO_NUM_25;
    int gpio_din = GPIO_NUM_22;
    
    // Channel configuration
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 8,
        .dma_frame_num = 64,
        .auto_clear = true,
    };
    
    // Initialize channel
    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(rx_handle);
    
    // Standard mode configuration
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = sample_rate,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = bit_width,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = channel_fmt,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_pol = false,
            .bit_shift = true,
            // Note: Other fields depend on ESP-IDF version
        },
        .gpio_cfg = {
            .mclk = GPIO_NUM_0,
            .bclk = gpio_bclk,
            .ws = gpio_ws,
            .din = gpio_din,
            .dout = GPIO_NUM_NC,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    // Configure standard mode
    ret = i2s_channel_init_std_mode(rx_handle, &std_cfg);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Enable/disable test
    ret = i2s_channel_enable(rx_handle);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Short delay to verify stability
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ret = i2s_channel_disable(rx_handle);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Clean up
    i2s_del_channel(rx_handle);
}

/**
 * @brief Run I2S audio driver tests
 */
void run_i2s_audio_tests(void)
{
    ESP_LOGI(TAG, "Starting I2S audio driver tests");
    
    // Begin Unity test session and explicitly register tests
    UNITY_BEGIN();
    
    // Explicitly run each test
    RUN_TEST(test_i2s_driver_init);
    RUN_TEST(test_i2s_standard_mode);
    
    // End Unity test session and display results
    int failures = UNITY_END();
    
    ESP_LOGI(TAG, "I2S audio driver tests completed with %d failures", failures);
}
