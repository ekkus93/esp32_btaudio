/* Stub driver/i2s_std.h for host tests (TODO 3.7).
 *
 * Just enough surface for i2s_out.c to compile: it only ever creates ONE
 * TX-only slave channel with a fixed Philips/32-bit-slot config, so the
 * struct layouts here don't need to match real ESP-IDF field-for-field —
 * they only need to accept exactly the field writes i2s_out.c performs.
 * Function bodies are provided per-test-file (mirrors the local-mock
 * convention already used for nvs.h/esp_heap_caps.h elsewhere in this
 * harness), so this header only declares.
 */
#ifndef STUB_DRIVER_I2S_STD_H
#define STUB_DRIVER_I2S_STD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef void *i2s_chan_handle_t;

typedef int i2s_port_t;
#define I2S_NUM_0 0

typedef int i2s_role_t;
#define I2S_ROLE_SLAVE 0
#define I2S_ROLE_MASTER 1

typedef int i2s_data_bit_width_t;
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_DATA_BIT_WIDTH_32BIT 32

typedef int i2s_slot_bit_width_t;
#define I2S_SLOT_BIT_WIDTH_32BIT 32

typedef int i2s_slot_mode_t;
#define I2S_SLOT_MODE_STEREO 2

#define I2S_GPIO_UNUSED (-1)

typedef struct {
    i2s_port_t id;
    i2s_role_t role;
    unsigned dma_desc_num;
    unsigned dma_frame_num;
    bool auto_clear;
} i2s_chan_config_t;

#define I2S_CHANNEL_DEFAULT_CONFIG(port, i2s_role) \
    { .id = (port), .role = (i2s_role), .dma_desc_num = 6, .dma_frame_num = 240, .auto_clear = false }

typedef struct {
    uint32_t sample_rate_hz;
    int      clk_src;
    int      mclk_multiple;
    int      bclk_div;
} i2s_std_clk_config_t;

#define I2S_STD_CLK_DEFAULT_CONFIG(rate) \
    { .sample_rate_hz = (rate), .clk_src = 0, .mclk_multiple = 256, .bclk_div = 8 }

typedef struct {
    i2s_data_bit_width_t data_bit_width;
    i2s_slot_bit_width_t slot_bit_width;
    i2s_slot_mode_t      slot_mode;
    int                  ws_width;
    bool                 ws_pol;
    bool                 bit_shift;
} i2s_std_slot_config_t;

#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, mode) \
    { .data_bit_width = (bits), .slot_bit_width = (bits), .slot_mode = (mode), \
      .ws_width = (bits), .ws_pol = false, .bit_shift = true }

typedef struct {
    bool mclk_inv;
    bool bclk_inv;
    bool ws_inv;
} i2s_std_gpio_invert_flags_t;

typedef struct {
    int mclk;
    int bclk;
    int ws;
    int dout;
    int din;
    i2s_std_gpio_invert_flags_t invert_flags;
} i2s_std_gpio_config_t;

typedef struct {
    i2s_std_clk_config_t  clk_cfg;
    i2s_std_slot_config_t slot_cfg;
    i2s_std_gpio_config_t gpio_cfg;
} i2s_std_config_t;

esp_err_t i2s_new_channel(const i2s_chan_config_t *chan_cfg,
                          i2s_chan_handle_t *tx_handle,
                          i2s_chan_handle_t *rx_handle);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t handle, const i2s_std_config_t *std_cfg);
esp_err_t i2s_channel_enable(i2s_chan_handle_t handle);
esp_err_t i2s_channel_disable(i2s_chan_handle_t handle);
esp_err_t i2s_channel_write(i2s_chan_handle_t handle, const void *src, size_t size,
                            size_t *bytes_written, TickType_t ticks_to_wait);
esp_err_t i2s_del_channel(i2s_chan_handle_t handle);

#endif
