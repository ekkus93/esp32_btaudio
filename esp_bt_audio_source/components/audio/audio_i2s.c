/**
 * @file audio_i2s.c
 * @brief I2S audio driver configuration for ESP32 Bluetooth Audio Source
 */

#include <string.h>
#include "esp_log.h"
#include "audio_i2s.h"
#include "driver/gpio.h"

static const char *TAG = "AUDIO_I2S";

static i2s_chan_handle_t rx_handle = NULL;
static bool is_initialized = false;
static bool is_running = false;

esp_err_t audio_i2s_init(const audio_i2s_config_t *config)
{
    if (is_initialized) {
        ESP_LOGW(TAG, "I2S already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;

    // Standard channel configuration
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = config->dma_buf_count,
        .dma_frame_num = config->dma_buf_len,
        .auto_clear = true,
    };
    
    // Initialize I2S channel for RX only (no TX needed)
    ESP_LOGI(TAG, "Creating I2S RX channel");
    ret = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %d", ret);
        return ret;
    }

    // Configure I2S standard mode
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = config->sample_rate,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = config->bit_width,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = config->channel_fmt,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_STD_WS_WIDTH_16_BCLK,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = false,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = config->gpio_mclk >= 0 ? config->gpio_mclk : GPIO_NUM_NC,
            .bclk = config->gpio_bclk,
            .ws = config->gpio_ws,
            .din = config->gpio_din,
            .dout = GPIO_NUM_NC, // Not used for input
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_LOGI(TAG, "Configuring I2S: %dHz, %d-bit, %s", 
             config->sample_rate,
             config->bit_width,
             config->channel_fmt == I2S_SLOT_MODE_MONO ? "mono" : "stereo");
    
    // Configure standard mode
    ret = i2s_channel_init_std_mode(rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S standard mode: %d", ret);
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
        return ret;
    }

    is_initialized = true;
    ESP_LOGI(TAG, "I2S driver initialized successfully");
    return ESP_OK;
}

esp_err_t audio_i2s_deinit(void)
{
    if (!is_initialized) {
        ESP_LOGW(TAG, "I2S not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (is_running) {
        audio_i2s_stop();
    }

    esp_err_t ret = i2s_del_channel(rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete I2S channel: %d", ret);
        return ret;
    }

    rx_handle = NULL;
    is_initialized = false;
    ESP_LOGI(TAG, "I2S driver deinitialized");
    return ESP_OK;
}

esp_err_t audio_i2s_start(void)
{
    if (!is_initialized) {
        ESP_LOGW(TAG, "I2S not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (is_running) {
        ESP_LOGW(TAG, "I2S already started");
        return ESP_OK;
    }

    esp_err_t ret = i2s_channel_enable(rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start I2S: %d", ret);
        return ret;
    }

    is_running = true;
    ESP_LOGI(TAG, "I2S started");
    return ESP_OK;
}

esp_err_t audio_i2s_stop(void)
{
    if (!is_initialized) {
        ESP_LOGW(TAG, "I2S not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!is_running) {
        ESP_LOGW(TAG, "I2S already stopped");
        return ESP_OK;
    }

    esp_err_t ret = i2s_channel_disable(rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop I2S: %d", ret);
        return ret;
    }

    is_running = false;
    ESP_LOGI(TAG, "I2S stopped");
    return ESP_OK;
}

esp_err_t audio_i2s_read(void *dest, size_t size, size_t *bytes_read, uint32_t timeout_ms)
{
    if (!is_initialized) {
        ESP_LOGW(TAG, "I2S not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!is_running) {
        ESP_LOGW(TAG, "I2S not started");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2s_channel_read(rx_handle, dest, size, bytes_read, timeout_ms);
    if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
        ESP_LOGE(TAG, "Failed to read from I2S: %d", ret);
    }

    return ret;
}
