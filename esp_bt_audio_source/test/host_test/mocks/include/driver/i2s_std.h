// Minimal stub of driver/i2s_std.h for host unit tests
#ifndef MOCK_DRIVER_I2S_STD_H
#define MOCK_DRIVER_I2S_STD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef void* i2s_chan_handle_t;

typedef enum {
    I2S_DATA_BIT_WIDTH_16BIT = 0,
    I2S_DATA_BIT_WIDTH_24BIT,
    I2S_DATA_BIT_WIDTH_32BIT
} i2s_data_bit_width_t;

typedef enum {
    I2S_SLOT_MODE_MONO = 0,
    I2S_SLOT_MODE_STEREO = 1,
} i2s_slot_mode_t;

typedef enum {
    I2S_SLOT_BIT_WIDTH_AUTO = 0,
} i2s_slot_bit_width_t;

typedef enum {
    I2S_ROLE_MASTER = 0,
} i2s_role_t;

typedef enum {
    I2S_CLK_SRC_DEFAULT = 0,
} i2s_clk_src_t;

typedef enum {
    I2S_MCLK_MULTIPLE_256 = 256,
} i2s_mclk_multiple_t;

typedef enum {
    I2S_STD_SLOT_BOTH = 0,
} i2s_std_slot_mask_t;

typedef struct {
    int id;
    i2s_role_t role;
    int dma_desc_num;
    int dma_frame_num;
    bool auto_clear;
} i2s_chan_config_t;

/* Common I2S port constants used in production code */
#define I2S_NUM_0 0
#define I2S_NUM_1 1

typedef struct {
    struct {
        uint32_t sample_rate_hz;
        i2s_clk_src_t clk_src;
        i2s_mclk_multiple_t mclk_multiple;
    } clk_cfg;
    struct {
        i2s_data_bit_width_t data_bit_width;
        i2s_slot_bit_width_t slot_bit_width;
        i2s_slot_mode_t slot_mode;
        i2s_std_slot_mask_t slot_mask;
        uint32_t ws_width;
        bool ws_pol;
        bool bit_shift;
    } slot_cfg;
    struct {
        int mclk;
        int bclk;
        int ws;
        int din;
        int dout;
        struct {
            bool mclk_inv;
            bool bclk_inv;
            bool ws_inv;
        } invert_flags;
    } gpio_cfg;
} i2s_std_config_t;

// Stubs for driver functions (implementations provided in mock_i2s.c)
int i2s_new_channel(const i2s_chan_config_t* chan_cfg, void* out_tx, i2s_chan_handle_t* handle);
int i2s_channel_init_std_mode(i2s_chan_handle_t handle, const i2s_std_config_t* cfg);
int i2s_channel_enable(i2s_chan_handle_t handle);
int i2s_channel_disable(i2s_chan_handle_t handle);
int i2s_del_channel(i2s_chan_handle_t handle);
int i2s_channel_read(i2s_chan_handle_t handle, void* data, size_t size, size_t* bytes_read, unsigned ticks_to_wait);

// Mock control helpers
void mock_i2s_std_reset_state(void);
void mock_i2s_std_set_next_read_result(int ret, size_t bytes);
void mock_i2s_std_set_next_new_result(int ret);
void mock_i2s_std_set_next_init_result(int ret);
void mock_i2s_std_set_next_enable_result(int ret);
void mock_i2s_std_set_next_disable_result(int ret);

#endif // MOCK_DRIVER_I2S_STD_H
