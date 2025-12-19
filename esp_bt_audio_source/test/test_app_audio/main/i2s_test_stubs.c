/*
 * Test-only I2S channel API stubs for unit test builds.
 * Provide minimal runtime behavior for the modern channel-based I2S API
 * so tests can exercise higher-level logic without pulling in the IDF driver
 * implementation (which expects a different ABI at runtime).
 */
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2s_std.h"

static const char *TAG = "i2s_test_stubs";

typedef struct {
    int id;
    bool enabled;
} stub_chan_t;

esp_err_t i2s_new_channel(const i2s_chan_config_t* cfg, const void* unused, i2s_chan_handle_t* out)
{
    if (!out || !cfg) return ESP_ERR_INVALID_ARG;
    stub_chan_t* s = (stub_chan_t*)malloc(sizeof(stub_chan_t));
    if (!s) return ESP_ERR_NO_MEM;
    s->id = cfg->id;
    s->enabled = false;
    *out = (i2s_chan_handle_t)s;
    ESP_LOGD(TAG, "i2s_new_channel: created stub channel id=%d", s->id);
    return ESP_OK;
}

esp_err_t i2s_channel_enable(i2s_chan_handle_t chan)
{
    if (!chan) return ESP_ERR_INVALID_ARG;
    stub_chan_t* s = (stub_chan_t*)chan;
    s->enabled = true;
    ESP_LOGD(TAG, "i2s_channel_enable: id=%d", s->id);
    return ESP_OK;
}

esp_err_t i2s_channel_disable(i2s_chan_handle_t chan)
{
    if (!chan) return ESP_ERR_INVALID_ARG;
    stub_chan_t* s = (stub_chan_t*)chan;
    s->enabled = false;
    ESP_LOGD(TAG, "i2s_channel_disable: id=%d", s->id);
    return ESP_OK;
}

esp_err_t i2s_del_channel(i2s_chan_handle_t chan)
{
    if (!chan) return ESP_ERR_INVALID_ARG;
    stub_chan_t* s = (stub_chan_t*)chan;
    ESP_LOGD(TAG, "i2s_del_channel: id=%d", s->id);
    free(s);
    return ESP_OK;
}

esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t chan, const i2s_std_config_t* cfg)
{
    if (!chan || !cfg) return ESP_ERR_INVALID_ARG;
    /* Accept config but do not attempt to validate fields deeply in tests. */
    ESP_LOGD(TAG, "i2s_channel_init_std_mode: init (stub)");
    return ESP_OK;
}

esp_err_t i2s_channel_read(i2s_chan_handle_t chan, void* buf, size_t size, size_t* bytes_read, int ticks_to_wait)
{
    if (!chan || !buf || size == 0) return ESP_ERR_INVALID_ARG;
    if (ticks_to_wait <= 1) {
        if (bytes_read) *bytes_read = 0;
        return ESP_ERR_TIMEOUT;
    }
    /* Fill with zeroes to simulate silence/read data. */
    memset(buf, 0, size);
    if (bytes_read) *bytes_read = size;
    return ESP_OK;
}
