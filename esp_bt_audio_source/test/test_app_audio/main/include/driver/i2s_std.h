/* Test-only shim for test_app_audio main include path */
#ifndef DRIVER_I2S_STD_H
#define DRIVER_I2S_STD_H
#include <stdbool.h>
#include "esp_err.h"
typedef int i2s_port_t;
#define I2S_NUM_0 0
typedef void* i2s_chan_handle_t;
typedef enum { I2S_ROLE_SLAVE = 0, I2S_ROLE_MASTER = 1 } i2s_role_t;
typedef enum {
	I2S_DATA_BIT_WIDTH_8BIT = 8,
	I2S_DATA_BIT_WIDTH_16BIT = 16,
	I2S_DATA_BIT_WIDTH_24BIT = 24,
	I2S_DATA_BIT_WIDTH_32BIT = 32
} i2s_data_bit_width_t;
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) (0)
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bit_width, slot_mode) (0)
/* Channel configuration used by tests (minimal, for build only) */
typedef struct {
	int id;
	int role; /* i2s_role_t */
	int dma_desc_num;
	int dma_frame_num;
	bool auto_clear;
} i2s_chan_config_t;

/* Minimal enums and macros referenced by the tests.
 * Use the same numeric values as the real I2S types so that
 * compiled test code and the audio component agree at runtime.
 */
typedef enum { I2S_SLOT_MODE_MONO = 1, I2S_SLOT_MODE_STEREO = 2 } i2s_slot_mode_t;
typedef enum {
	I2S_SLOT_BIT_WIDTH_AUTO = 0,
	I2S_SLOT_BIT_WIDTH_32BIT = 32
} i2s_std_slot_bit_width_t;
typedef enum { I2S_CLK_SRC_DEFAULT = 0 } i2s_clk_src_t;
#define I2S_MCLK_MULTIPLE_256 256
#define I2S_STD_SLOT_LEFT 1
#define I2S_STD_SLOT_RIGHT 2
#define I2S_STD_SLOT_BOTH (I2S_STD_SLOT_LEFT | I2S_STD_SLOT_RIGHT)

/* (i2s_data_bit_width_t already declared above) */

/* Standard mode configuration with nested structs matching test usage */
typedef struct {
	uint32_t sample_rate_hz;
	i2s_clk_src_t clk_src;
	int mclk_multiple;
} i2s_std_clk_cfg_t;

typedef struct {
	i2s_data_bit_width_t data_bit_width;
	i2s_std_slot_bit_width_t slot_bit_width;
	i2s_slot_mode_t slot_mode;
	int slot_mask;
	int ws_width;
	bool ws_pol;
	bool bit_shift;
} i2s_std_slot_cfg_t;

typedef struct {
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
} i2s_std_gpio_cfg_t;

typedef struct {
	i2s_std_clk_cfg_t clk_cfg;
	i2s_std_slot_cfg_t slot_cfg;
	i2s_std_gpio_cfg_t gpio_cfg;
} i2s_std_config_t;
esp_err_t i2s_new_channel(const i2s_chan_config_t* cfg, const void* unused, i2s_chan_handle_t* out);
esp_err_t i2s_channel_enable(i2s_chan_handle_t chan);
esp_err_t i2s_channel_disable(i2s_chan_handle_t chan);
esp_err_t i2s_del_channel(i2s_chan_handle_t chan);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t chan, const i2s_std_config_t* cfg);
esp_err_t i2s_channel_read(i2s_chan_handle_t chan, void* buf, size_t size, size_t* bytes_read, int ticks_to_wait);
#endif
