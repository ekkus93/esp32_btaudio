#include "bt_hfp_audio.h"

static esp_err_t s_register_result = ESP_OK;
static esp_err_t s_cleanup_result = ESP_OK;
static unsigned s_register_calls;
static unsigned s_stopping_calls;
static unsigned s_cleanup_calls;

void mock_bt_hfp_audio_lifecycle_reset(void)
{
    s_register_result = ESP_OK;
    s_cleanup_result = ESP_OK;
    s_register_calls = 0U;
    s_stopping_calls = 0U;
    s_cleanup_calls = 0U;
}

void mock_bt_hfp_audio_set_register_result(esp_err_t result)
{
    s_register_result = result;
}

void mock_bt_hfp_audio_set_cleanup_result(esp_err_t result)
{
    s_cleanup_result = result;
    /* Begin a new explicitly configured cleanup observation window. */
    s_cleanup_calls = 0U;
}

unsigned mock_bt_hfp_audio_register_calls(void)
{
    return s_register_calls;
}

unsigned mock_bt_hfp_audio_stopping_calls(void)
{
    return s_stopping_calls;
}

unsigned mock_bt_hfp_audio_cleanup_calls(void)
{
    return s_cleanup_calls;
}

esp_err_t bt_hfp_audio_register_callback(void)
{
    s_register_calls++;
    return s_register_result;
}

void bt_hfp_audio_profile_stopping(void)
{
    s_stopping_calls++;
}

void bt_hfp_audio_control_profile_stopping(void)
{
    /* The production control hook closes the fast callback gate itself. Keep
     * one observable lifecycle count for the existing AG assertions. */
    s_stopping_calls++;
}

esp_err_t bt_hfp_audio_control_cleanup_after_stack_shutdown(void)
{
    return ESP_OK;
}

esp_err_t bt_hfp_audio_cleanup_after_stack_shutdown(void)
{
    s_cleanup_calls++;
    return s_cleanup_result;
}
