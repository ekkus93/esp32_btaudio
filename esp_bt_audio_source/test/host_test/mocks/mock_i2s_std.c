// Mock implementation of driver/i2s_std for host tests
#include <string.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2s_std.h"

typedef struct {
    bool created;
    bool enabled;
    int last_id;
    esp_err_t next_read_ret;
    size_t next_read_bytes;
} mock_i2s_state_t;

static mock_i2s_state_t s_i2s_state = {
    .created = false,
    .enabled = false,
    .last_id = -1,
    .next_read_ret = ESP_OK,
    .next_read_bytes = 0
};

void mock_i2s_std_reset_state(void)
{
    s_i2s_state.created = false;
    s_i2s_state.enabled = false;
    s_i2s_state.last_id = -1;
    s_i2s_state.next_read_ret = ESP_OK;
    s_i2s_state.next_read_bytes = 0;
}

void mock_i2s_std_set_next_read_result(esp_err_t ret, size_t bytes)
{
    s_i2s_state.next_read_ret = ret;
    s_i2s_state.next_read_bytes = bytes;
}

int i2s_new_channel(const i2s_chan_config_t* chan_cfg, void* out_tx, i2s_chan_handle_t* handle)
{
    (void)out_tx;
    if (!chan_cfg || !handle) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_i2s_state.created) {
        return ESP_ERR_INVALID_STATE;
    }
    s_i2s_state.created = true;
    s_i2s_state.enabled = false;
    s_i2s_state.last_id = chan_cfg->id;
    *handle = (i2s_chan_handle_t)0x1;
    return ESP_OK;
}

int i2s_channel_init_std_mode(i2s_chan_handle_t handle, const i2s_std_config_t* cfg)
{
    if (!handle || !cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    return s_i2s_state.created ? ESP_OK : ESP_ERR_INVALID_STATE;
}

int i2s_channel_enable(i2s_chan_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_i2s_state.created) {
        return ESP_ERR_INVALID_STATE;
    }
    s_i2s_state.enabled = true;
    return ESP_OK;
}

int i2s_channel_disable(i2s_chan_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_i2s_state.created || !s_i2s_state.enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    s_i2s_state.enabled = false;
    return ESP_OK;
}

int i2s_del_channel(i2s_chan_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_i2s_state.created) {
        return ESP_ERR_INVALID_STATE;
    }
    s_i2s_state.created = false;
    s_i2s_state.enabled = false;
    s_i2s_state.last_id = -1;
    return ESP_OK;
}

int i2s_channel_read(i2s_chan_handle_t handle, void* data, size_t size, size_t* bytes_read, unsigned ticks_to_wait)
{
    (void)ticks_to_wait;
    if (!handle || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_i2s_state.created || !s_i2s_state.enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (bytes_read) {
        *bytes_read = s_i2s_state.next_read_bytes;
    }
    return s_i2s_state.next_read_ret;
}
