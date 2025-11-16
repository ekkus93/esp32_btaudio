// Minimal stub of driver/i2s_std.h for host unit tests
#ifndef MOCK_DRIVER_I2S_STD_H
#define MOCK_DRIVER_I2S_STD_H

#include <stdint.h>

typedef void* i2s_chan_handle_t;

typedef enum {
    I2S_DATA_BIT_WIDTH_16BIT = 0,
    I2S_DATA_BIT_WIDTH_24BIT,
    I2S_DATA_BIT_WIDTH_32BIT
} i2s_data_bit_width_t;

typedef struct {
    int id;
    int role;
    int dma_desc_num;
    int dma_frame_num;
    int auto_clear;
} i2s_chan_config_t;

/* Common I2S port constants used in production code */
#define I2S_NUM_0 0
#define I2S_NUM_1 1

typedef struct {
    int clk_cfg;
    struct {
        i2s_data_bit_width_t bit_width;
        int slot_mode;
    } slot_cfg;
    struct {
        int mclk;
        int bclk;
        int ws;
        int din;
        int dout;
    } gpio_cfg;
} i2s_std_config_t;

// Stubs for driver functions (implementations provided in mock_i2s.c)
int i2s_new_channel(const i2s_chan_config_t* chan_cfg, void* out_tx, i2s_chan_handle_t* handle);
int i2s_channel_init_std_mode(i2s_chan_handle_t handle, const i2s_std_config_t* cfg);
int i2s_channel_enable(i2s_chan_handle_t handle);
int i2s_channel_disable(i2s_chan_handle_t handle);
int i2s_del_channel(i2s_chan_handle_t handle);

#endif // MOCK_DRIVER_I2S_STD_H
