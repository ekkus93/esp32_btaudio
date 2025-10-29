#include <string.h>
#include "esp_err.h"
#include "bt_api.h"
#include "bt_source.h"
#include "bt_connection_shim.h"

// Minimal shim implementations to satisfy tests that register callbacks.
// These are intentionally small; if tests require richer behavior we can
// replace these with the full mock implementation from test_app.

static bt_connection_callback_t s_conn_cb = NULL;
static void *s_conn_cb_data = NULL;

static bt_stream_callback_t s_stream_cb = NULL;
static void *s_stream_cb_data = NULL;

static bt_connection_info_t s_cached_info = {0};
static bool s_info_valid = false;

esp_err_t bt_register_connection_callback(bt_connection_callback_t callback, void* user_data)
{
    s_conn_cb = callback;
    s_conn_cb_data = user_data;
    if (s_conn_cb && s_info_valid) {
        bt_connection_info_t info = s_cached_info;
        s_conn_cb(&info, s_conn_cb_data);
    }
    return ESP_OK;
}

esp_err_t bt_register_streaming_callback(bt_stream_callback_t callback, void* user_data)
{
    s_stream_cb = callback;
    s_stream_cb_data = user_data;
    return ESP_OK;
}

esp_err_t bt_get_connection_info(bt_connection_info_t* info)
{
    if (!info) return ESP_ERR_INVALID_ARG;
    bt_connection_shim_get_cached_info(info);
    return ESP_OK;
}

void bt_connection_shim_publish_info(const bt_connection_info_t *info)
{
    if (!info) {
        return;
    }

    s_cached_info = *info;
    s_info_valid = true;

    if (s_conn_cb) {
        bt_connection_info_t cb_info = s_cached_info;
        s_conn_cb(&cb_info, s_conn_cb_data);
    }
}

void bt_connection_shim_clear_info(void)
{
    memset(&s_cached_info, 0, sizeof(s_cached_info));
    s_cached_info.state = BT_CONNECTION_STATE_DISCONNECTED;
    s_info_valid = false;
}

void bt_connection_shim_get_cached_info(bt_connection_info_t *info)
{
    if (!info) {
        return;
    }

    if (s_info_valid) {
        *info = s_cached_info;
    } else {
        memset(info, 0, sizeof(*info));
        info->state = BT_CONNECTION_STATE_DISCONNECTED;
    }
}

bool bt_connection_shim_callback_registered(void)
{
    return (s_conn_cb != NULL);
}
