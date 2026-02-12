#include "mock_avrc.h"
#include "esp_avrc_api.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

static struct {
    bool init_called;
    bool deinit_called;
    esp_avrc_ct_cb_t registered_cb;
    esp_err_t init_result;
    esp_err_t callback_result;
} s_avrc_state = {
    .init_result = ESP_OK,
    .callback_result = ESP_OK,
};

// Shared test hook for call ordering (weak in case tests don't provide it)
__attribute__((weak)) void mock_bt_call_log(const char* tag) {
    (void)tag;
}

void mock_avrc_reset(void)
{
    s_avrc_state.init_called = false;
    s_avrc_state.deinit_called = false;
    s_avrc_state.registered_cb = NULL;
    s_avrc_state.init_result = ESP_OK;
    s_avrc_state.callback_result = ESP_OK;
}

void mock_avrc_set_init_result(esp_err_t result)
{
    s_avrc_state.init_result = result;
}

void mock_avrc_set_callback_result(esp_err_t result)
{
    s_avrc_state.callback_result = result;
}

bool mock_avrc_was_init_called(void)
{
    return s_avrc_state.init_called;
}

bool mock_avrc_was_deinit_called(void)
{
    return s_avrc_state.deinit_called;
}

esp_avrc_ct_cb_t mock_avrc_get_registered_callback(void)
{
    return s_avrc_state.registered_cb;
}

esp_err_t esp_avrc_ct_init(void)
{
    s_avrc_state.init_called = true;
    mock_bt_call_log("esp_avrc_ct_init");
    return s_avrc_state.init_result;
}

esp_err_t esp_avrc_ct_register_callback(esp_avrc_ct_cb_t callback)
{
    s_avrc_state.registered_cb = callback;
    mock_bt_call_log("esp_avrc_ct_register_callback");
    return s_avrc_state.callback_result;
}

esp_err_t esp_avrc_ct_deinit(void)
{
    s_avrc_state.deinit_called = true;
    return ESP_OK;
}
