#include "esp_err.h"
#include "bt_api.h"
#include "bt_source.h"

// Minimal shim implementations to satisfy tests that register callbacks.
// These are intentionally small; if tests require richer behavior we can
// replace these with the full mock implementation from test_app.

static bt_connection_callback_t s_conn_cb = NULL;
static void *s_conn_cb_data = NULL;

static bt_stream_callback_t s_stream_cb = NULL;
static void *s_stream_cb_data = NULL;

esp_err_t bt_register_connection_callback(bt_connection_callback_t callback, void* user_data)
{
    s_conn_cb = callback;
    s_conn_cb_data = user_data;
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
    // default: not connected
    info->connected = false;
    info->addr[0] = '\0';
    info->name[0] = '\0';
    info->streaming = false;
    info->state = BT_CONNECTION_STATE_DISCONNECTED;
    info->retry_count = 0;
    return ESP_OK;
}
