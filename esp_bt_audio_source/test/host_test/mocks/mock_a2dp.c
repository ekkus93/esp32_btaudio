#include <stdio.h>
#include <string.h>
#include "mock_a2dp.h"

#define MAC_STRING_LEN 18

static struct {
    esp_bt_status_t connect_result;
    esp_bt_status_t disconnect_result;
    esp_bt_status_t init_result;
    esp_bt_status_t deinit_result;
    esp_bt_status_t media_ctrl_result;
    esp_a2d_media_ctrl_t last_media_ctrl;
    int connect_calls;
    int disconnect_calls;
    int media_ctrl_calls;
    char last_connect_addr[MAC_STRING_LEN];
    char last_disconnect_addr[MAC_STRING_LEN];
    bool init_called;
    bool deinit_called;
    esp_a2d_cb_t registered_cb;
    esp_a2d_source_data_cb_t registered_data_cb;
} s_a2dp_state = {
    .connect_result = ESP_BT_STATUS_SUCCESS,
    .disconnect_result = ESP_BT_STATUS_SUCCESS,
    .init_result = ESP_BT_STATUS_SUCCESS,
    .deinit_result = ESP_BT_STATUS_SUCCESS,
    .media_ctrl_result = ESP_BT_STATUS_SUCCESS,
    .last_media_ctrl = ESP_A2D_MEDIA_CTRL_STOP,
};

// Optional test hook to record call order; weak so other tests unaffected
__attribute__((weak)) void mock_bt_call_log(const char* tag) {
    (void)tag;
}

static void format_mac(const esp_bd_addr_t addr, char *out)
{
    if (!out) {
        return;
    }
    if (!addr) {
        out[0] = '\0';
        return;
    }
    snprintf(out, MAC_STRING_LEN, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

void mock_a2dp_reset(void)
{
    s_a2dp_state.connect_result = ESP_BT_STATUS_SUCCESS;
    s_a2dp_state.disconnect_result = ESP_BT_STATUS_SUCCESS;
    s_a2dp_state.init_result = ESP_BT_STATUS_SUCCESS;
    s_a2dp_state.deinit_result = ESP_BT_STATUS_SUCCESS;
    s_a2dp_state.media_ctrl_result = ESP_BT_STATUS_SUCCESS;
    s_a2dp_state.last_media_ctrl = ESP_A2D_MEDIA_CTRL_STOP;
    s_a2dp_state.connect_calls = 0;
    s_a2dp_state.disconnect_calls = 0;
    s_a2dp_state.media_ctrl_calls = 0;
    s_a2dp_state.last_connect_addr[0] = '\0';
    s_a2dp_state.last_disconnect_addr[0] = '\0';
    s_a2dp_state.init_called = false;
    s_a2dp_state.deinit_called = false;
    s_a2dp_state.registered_cb = NULL;
    s_a2dp_state.registered_data_cb = NULL;
}

void mock_a2dp_set_connect_result(esp_bt_status_t result)
{
    s_a2dp_state.connect_result = result;
}

void mock_a2dp_set_disconnect_result(esp_bt_status_t result)
{
    s_a2dp_state.disconnect_result = result;
}

void mock_a2dp_set_media_ctrl_result(esp_bt_status_t result)
{
    s_a2dp_state.media_ctrl_result = result;
}

void mock_a2dp_set_init_result(esp_bt_status_t result)
{
    s_a2dp_state.init_result = result;
}

void mock_a2dp_set_deinit_result(esp_bt_status_t result)
{
    s_a2dp_state.deinit_result = result;
}

int mock_a2dp_get_connect_calls(void)
{
    return s_a2dp_state.connect_calls;
}

int mock_a2dp_get_disconnect_calls(void)
{
    return s_a2dp_state.disconnect_calls;
}

const char *mock_a2dp_get_last_connect_addr(void)
{
    return s_a2dp_state.last_connect_addr;
}

const char *mock_a2dp_get_last_disconnect_addr(void)
{
    return s_a2dp_state.last_disconnect_addr;
}

int mock_a2dp_get_media_ctrl_calls(void)
{
    return s_a2dp_state.media_ctrl_calls;
}

esp_a2d_media_ctrl_t mock_a2dp_get_last_media_ctrl(void)
{
    return s_a2dp_state.last_media_ctrl;
}

bool mock_a2dp_was_init_called(void)
{
    return s_a2dp_state.init_called;
}

bool mock_a2dp_was_deinit_called(void)
{
    return s_a2dp_state.deinit_called;
}

esp_a2d_cb_t mock_a2dp_get_registered_callback(void)
{
    return s_a2dp_state.registered_cb;
}

esp_a2d_source_data_cb_t mock_a2dp_get_registered_data_callback(void)
{
    return s_a2dp_state.registered_data_cb;
}

esp_bt_status_t esp_a2d_source_init(void)
{
    s_a2dp_state.init_called = true;
    mock_bt_call_log("esp_a2d_source_init");
    return s_a2dp_state.init_result;
}

esp_bt_status_t esp_a2d_source_deinit(void)
{
    s_a2dp_state.deinit_called = true;
    return s_a2dp_state.deinit_result;
}

esp_bt_status_t esp_a2d_source_connect(esp_bd_addr_t remote_bda)
{
    format_mac(remote_bda, s_a2dp_state.last_connect_addr);
    s_a2dp_state.connect_calls++;
    return s_a2dp_state.connect_result;
}

esp_bt_status_t esp_a2d_source_disconnect(esp_bd_addr_t remote_bda)
{
    format_mac(remote_bda, s_a2dp_state.last_disconnect_addr);
    s_a2dp_state.disconnect_calls++;
    return s_a2dp_state.disconnect_result;
}

esp_bt_status_t esp_a2d_media_ctrl(esp_a2d_media_ctrl_t ctrl)
{
    s_a2dp_state.last_media_ctrl = ctrl;
    s_a2dp_state.media_ctrl_calls++;
    return s_a2dp_state.media_ctrl_result;
}

esp_bt_status_t esp_a2d_register_callback(esp_a2d_cb_t callback)
{
    s_a2dp_state.registered_cb = callback;
    mock_bt_call_log("esp_a2d_register_callback");
    return ESP_BT_STATUS_SUCCESS;
}

esp_bt_status_t esp_a2d_source_register_data_callback(esp_a2d_source_data_cb_t callback)
{
    s_a2dp_state.registered_data_cb = callback;
    mock_bt_call_log("esp_a2d_source_register_data_callback");
    return ESP_BT_STATUS_SUCCESS;
}
