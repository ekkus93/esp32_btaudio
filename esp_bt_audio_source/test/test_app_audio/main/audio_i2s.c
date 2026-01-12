#include "audio_i2s.h"

static audio_i2s_config_t s_cfg = {0};
static bool s_initialized = false;
static bool s_started = false;

esp_err_t audio_i2s_init(const audio_i2s_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_cfg = *config;
    s_started = false;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t audio_i2s_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_started = true;
    return ESP_OK;
}

esp_err_t audio_i2s_stop(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_started = false;
    return ESP_OK;
}

esp_err_t audio_i2s_deinit(void)
{
    s_started = false;
    s_initialized = false;
    s_cfg = (audio_i2s_config_t){0};
    return ESP_OK;
}

esp_err_t audio_i2s_read(uint8_t *dest, size_t len, size_t *bytes_read, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!dest) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0) {
        if (bytes_read) {
            *bytes_read = 0;
        }
        return ESP_OK;
    }
    if (bytes_read) {
        *bytes_read = 0;
    }
    return ESP_ERR_TIMEOUT;
}
