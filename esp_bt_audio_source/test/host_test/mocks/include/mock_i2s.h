#ifndef MOCK_I2S_H
#define MOCK_I2S_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Minimal mock I2S types and APIs used by host tests */
#if !defined(_AUDIO_PROCESSOR_H_) && !defined(__I2S_PORT_T_DEFINED)
typedef enum {
    I2S_NUM_0 = 0,
    I2S_NUM_1 = 1,
    I2S_NUM_MAX
} i2s_port_t;
#define __I2S_PORT_T_DEFINED
#endif

typedef enum {
    I2S_COMM_FORMAT_STAND_I2S = 0x01,
    I2S_COMM_FORMAT_STAND_MSB = 0x02,
    I2S_COMM_FORMAT_STAND_PCM_SHORT = 0x04,
    I2S_COMM_FORMAT_STAND_PCM_LONG = 0x08,
    I2S_COMM_FORMAT_STAND_MAX,
} i2s_comm_format_t;

typedef enum {
    I2S_CHANNEL_FMT_RIGHT_LEFT = 0x00,
    I2S_CHANNEL_FMT_ALL_RIGHT,
    I2S_CHANNEL_FMT_ALL_LEFT,
    I2S_CHANNEL_FMT_ONLY_RIGHT,
    I2S_CHANNEL_FMT_ONLY_LEFT,
} i2s_channel_fmt_t;

typedef enum {
    I2S_BITS_PER_SAMPLE_8BIT = 8,
    I2S_BITS_PER_SAMPLE_16BIT = 16,
    I2S_BITS_PER_SAMPLE_24BIT = 24,
    I2S_BITS_PER_SAMPLE_32BIT = 32,
} i2s_bits_per_sample_t;

typedef enum {
    I2S_MODE_MASTER = 1,
    I2S_MODE_SLAVE = 2,
    I2S_MODE_TX = 4,
    I2S_MODE_RX = 8,
} i2s_mode_t;

typedef struct {
    i2s_mode_t mode;
    int sample_rate;
    i2s_bits_per_sample_t bits_per_sample;
    i2s_channel_fmt_t channel_format;
    i2s_comm_format_t communication_format;
    int intr_alloc_flags;
    int dma_buf_count;
    int dma_buf_len;
    bool use_apll;
    bool tx_desc_auto_clear;
    int fixed_mclk;
} i2s_config_t;

typedef struct {
    int bck_io_num;
    int ws_io_num;
    int data_out_num;
    int data_in_num;
} i2s_pin_config_t;

typedef int esp_err_t; /* allow use without esp_err.h in some compilation units */

esp_err_t i2s_driver_install(i2s_port_t i2s_num, const i2s_config_t *i2s_config,
                            int queue_size, void *i2s_queue);
esp_err_t i2s_driver_uninstall(i2s_port_t i2s_num);
esp_err_t i2s_set_pin(i2s_port_t i2s_num, const i2s_pin_config_t *pin);
esp_err_t i2s_start(i2s_port_t i2s_num);
esp_err_t i2s_stop(i2s_port_t i2s_num);
esp_err_t i2s_read(i2s_port_t i2s_num, void *dest, size_t size,
                  size_t *bytes_read, unsigned int ticks_to_wait);
esp_err_t i2s_write(i2s_port_t i2s_num, const void *src, size_t size,
                   size_t *bytes_written, unsigned int ticks_to_wait);

bool mock_i2s_is_initialized(i2s_port_t i2s_num);
bool mock_i2s_is_running(i2s_port_t i2s_num);
i2s_config_t* mock_i2s_get_config(i2s_port_t i2s_num);
i2s_pin_config_t* mock_i2s_get_pins(i2s_port_t i2s_num);

#endif /* MOCK_I2S_H */
