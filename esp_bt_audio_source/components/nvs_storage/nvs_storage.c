#include "nvs_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include "esp_log.h"

static const char* TAG = "nvs_storage";
static const char* NVS_NAMESPACE = "bt_audio_cfg";

esp_err_t nvs_storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t nvs_storage_get_volume(uint8_t* volume)
{
    if (!volume) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    int32_t v = 0;
    err = nvs_get_i32(h, "volume", &v);
    if (err == ESP_OK) *volume = (uint8_t)v;
    nvs_close(h);
    return err == ESP_OK ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t nvs_storage_set_volume(uint8_t volume)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    int32_t v = volume;
    err = nvs_set_i32(h, "volume", v);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t nvs_storage_get_i2s_pins(int* bclk, int* ws, int* din, int* dout)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    int32_t tmp;
    if (bclk) {
        err = nvs_get_i32(h, "i2s_bclk", &tmp);
        if (err == ESP_OK) *bclk = (int)tmp; else *bclk = -1;
    }
    if (ws) {
        err = nvs_get_i32(h, "i2s_ws", &tmp);
        if (err == ESP_OK) *ws = (int)tmp; else *ws = -1;
    }
    if (din) {
        err = nvs_get_i32(h, "i2s_din", &tmp);
        if (err == ESP_OK) *din = (int)tmp; else *din = -1;
    }
    if (dout) {
        err = nvs_get_i32(h, "i2s_dout", &tmp);
        if (err == ESP_OK) *dout = (int)tmp; else *dout = -1;
    }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t nvs_storage_set_i2s_pins(int bclk, int ws, int din, int dout)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_i32(h, "i2s_bclk", bclk);
    if (err == ESP_OK) err = nvs_set_i32(h, "i2s_ws", ws);
    if (err == ESP_OK) err = nvs_set_i32(h, "i2s_din", din);
    if (err == ESP_OK) err = nvs_set_i32(h, "i2s_dout", dout);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t nvs_storage_get_device_name(char* buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t required = buf_len;
    err = nvs_get_str(h, "device_name", buf, &required);
    nvs_close(h);
    return err;
}

esp_err_t nvs_storage_set_device_name(const char* name)
{
    if (!name) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, "device_name", name);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t nvs_storage_get_default_pin(char* buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t required = buf_len;
    err = nvs_get_str(h, "default_pin", buf, &required);
    nvs_close(h);
    return err;
}

esp_err_t nvs_storage_set_default_pin(const char* pin)
{
    if (!pin) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, "default_pin", pin);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
