#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

static const char *TAG = "I2S_TEST";

static void test_i2s_driver_init(void)
{
    i2s_chan_handle_t rx_handle = NULL;
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 8,
        .dma_frame_num = 64,
        .auto_clear = true,
    };
    ESP_LOGI(TAG, "Testing I2S channel initialization");
    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(rx_handle);
    if (rx_handle) {
        i2s_del_channel(rx_handle);
    }
}

static void test_i2s_std_config(void)
{
    i2s_chan_handle_t rx_handle = NULL;
    uint32_t sample_rate = 44100;
    i2s_data_bit_width_t bit_width = I2S_DATA_BIT_WIDTH_16BIT;
    i2s_slot_mode_t channel_fmt = I2S_SLOT_MODE_STEREO;
    int gpio_bclk = GPIO_NUM_26;
    int gpio_ws = GPIO_NUM_25;
    int gpio_din = GPIO_NUM_22;
    ESP_LOGI(TAG, "Testing I2S standard config: %" PRIu32 "Hz, %d-bit, %s",
             sample_rate,
             (int)bit_width,
             channel_fmt == I2S_SLOT_MODE_MONO ? "mono" : "stereo");
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
            .ws_width = 0,
            .ws_pol = false,
            .bit_shift = true,
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
    ret = i2s_channel_init_std_mode(rx_handle, &std_cfg);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    ret = i2s_channel_enable(rx_handle);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    vTaskDelay(pdMS_TO_TICKS(100));
    ret = i2s_channel_disable(rx_handle);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    i2s_del_channel(rx_handle);
}

void run_i2s_tests(void)
{
    ESP_LOGI(TAG, "Starting I2S driver tests for audio input");
    RUN_TEST(test_i2s_driver_init);
    RUN_TEST(test_i2s_std_config);
    ESP_LOGI(TAG, "I2S driver tests completed");
}
