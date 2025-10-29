/* Test-only shim placed in test_app/main/include so the compiler finds it via
 * the existing include paths used for the Unity test app. This duplicates the
 * lightweight compatibility header in components but sits in an include path
 * that is always visible while compiling tests.
 */
#ifndef DRIVER_I2S_STD_H
#define DRIVER_I2S_STD_H

#include <stdbool.h>
#include "esp_err.h"

typedef int i2s_port_t;
#define I2S_NUM_0 0
typedef void* i2s_chan_handle_t;
typedef enum { I2S_ROLE_SLAVE = 0, I2S_ROLE_MASTER = 1 } i2s_role_t;
typedef enum { I2S_DATA_BIT_WIDTH_16BIT = 0, I2S_DATA_BIT_WIDTH_24BIT = 1, I2S_DATA_BIT_WIDTH_32BIT = 2 } i2s_data_bit_width_t;
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) (0)
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bit_width, slot_mode) (0)
typedef struct { int dummy; } i2s_chan_config_t;
typedef struct { int dummy; } i2s_std_config_t;
/* Provide GPIO_NUM_NC used by test stubs */
#ifndef GPIO_NUM_NC
#define GPIO_NUM_NC (-1)
#endif
esp_err_t i2s_new_channel(const i2s_chan_config_t* cfg, const void* unused, i2s_chan_handle_t* out);
esp_err_t i2s_channel_enable(i2s_chan_handle_t chan);
esp_err_t i2s_channel_disable(i2s_chan_handle_t chan);
esp_err_t i2s_del_channel(i2s_chan_handle_t chan);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t chan, const i2s_std_config_t* cfg);
esp_err_t i2s_channel_read(i2s_chan_handle_t chan, void* buf, size_t size, size_t* bytes_read, int ticks_to_wait);

#endif /* DRIVER_I2S_STD_H */
