/**
 * @file audio_i2s.h
 * @brief I2S audio driver configuration for ESP32 Bluetooth Audio Source
 */

#ifndef AUDIO_I2S_H
#define AUDIO_I2S_H

#include "esp_err.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

/**
 * @brief I2S audio configuration parameters
 */
typedef struct {
    uint32_t sample_rate;          /*!< Sample rate in Hz (e.g., 44100, 48000) */
    i2s_data_bit_width_t bit_width; /*!< Bit width of audio data (16/24/32) */
    i2s_slot_mode_t channel_fmt;   /*!< Channel format (mono/stereo) */
    int gpio_bclk;                 /*!< GPIO for I2S bit clock */
    int gpio_ws;                   /*!< GPIO for I2S word select */
    int gpio_din;                  /*!< GPIO for I2S data input */
    int gpio_mclk;                 /*!< GPIO for I2S master clock (optional, set -1 if not used) */
    size_t dma_buf_count;          /*!< Number of DMA buffers */
    size_t dma_buf_len;            /*!< Size of each DMA buffer in samples */
} audio_i2s_config_t;

/**
 * @brief Default I2S audio configuration
 */
#define AUDIO_I2S_DEFAULT_CONFIG() { \
    .sample_rate = 44100,          \
    .bit_width = I2S_DATA_BIT_WIDTH_16BIT, \
    .channel_fmt = I2S_SLOT_MODE_STEREO, \
    .gpio_bclk = GPIO_NUM_26,      \
    .gpio_ws = GPIO_NUM_25,        \
    .gpio_din = GPIO_NUM_22,       \
    .gpio_mclk = -1,               \
    .dma_buf_count = 8,            \
    .dma_buf_len = 64              \
}

/**
 * @brief Initialize I2S driver for audio input
 * 
 * @param config The I2S configuration parameters
 * @return ESP_OK on success, or an error code
 */
esp_err_t audio_i2s_init(const audio_i2s_config_t *config);

/**
 * @brief Deinitialize I2S driver
 * 
 * @return ESP_OK on success, or an error code
 */
esp_err_t audio_i2s_deinit(void);

/**
 * @brief Start I2S audio reception
 * 
 * @return ESP_OK on success, or an error code
 */
esp_err_t audio_i2s_start(void);

/**
 * @brief Stop I2S audio reception
 * 
 * @return ESP_OK on success, or an error code
 */
esp_err_t audio_i2s_stop(void);

/**
 * @brief Read audio data from I2S
 * 
 * @param dest Destination buffer for audio data
 * @param size Size of data to read in bytes
 * @param[out] bytes_read Actual number of bytes read
 * @param timeout_ms Timeout in milliseconds, or 0 for no timeout
 * @return ESP_OK on success, or an error code
 */
esp_err_t audio_i2s_read(void *dest, size_t size, size_t *bytes_read, uint32_t timeout_ms);

#endif /* AUDIO_I2S_H */
