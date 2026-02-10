/**
 * @file platform_storage_esp32.c
 * @brief ESP32 implementation of platform storage API (wraps NVS)
 */

#include "platform_storage.h"
#include "nvs_flash.h"
#include "nvs.h"

esp_err_t platform_storage_init(void) {
    return nvs_flash_init();
}

esp_err_t platform_storage_erase(void) {
    return nvs_flash_erase();
}

esp_err_t platform_storage_open(const char* namespace, platform_storage_mode_t mode, platform_storage_handle_t* handle) {
    nvs_open_mode_t nvs_mode = (mode == PLATFORM_STORAGE_READONLY) ? NVS_READONLY : NVS_READWRITE;
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(namespace, nvs_mode, &nvs_handle);
    if (err == ESP_OK) {
        *handle = (platform_storage_handle_t)nvs_handle;
    }
    return err;
}

esp_err_t platform_storage_close(platform_storage_handle_t handle) {
    nvs_close((nvs_handle_t)handle);
    return ESP_OK;
}

esp_err_t platform_storage_get_i32(platform_storage_handle_t handle, const char* key, int32_t* out_value) {
    return nvs_get_i32((nvs_handle_t)handle, key, out_value);
}

esp_err_t platform_storage_set_i32(platform_storage_handle_t handle, const char* key, int32_t value) {
    return nvs_set_i32((nvs_handle_t)handle, key, value);
}

esp_err_t platform_storage_get_str(platform_storage_handle_t handle, const char* key, char* out_str, size_t* length) {
    return nvs_get_str((nvs_handle_t)handle, key, out_str, length);
}

esp_err_t platform_storage_set_str(platform_storage_handle_t handle, const char* key, const char* value) {
    return nvs_set_str((nvs_handle_t)handle, key, value);
}

esp_err_t platform_storage_get_blob(platform_storage_handle_t handle, const char* key, void* out_value, size_t* length) {
    return nvs_get_blob((nvs_handle_t)handle, key, out_value, length);
}

esp_err_t platform_storage_set_blob(platform_storage_handle_t handle, const char* key, const void* value, size_t length) {
    return nvs_set_blob((nvs_handle_t)handle, key, value, length);
}

esp_err_t platform_storage_erase_key(platform_storage_handle_t handle, const char* key) {
    return nvs_erase_key((nvs_handle_t)handle, key);
}

esp_err_t platform_storage_commit(platform_storage_handle_t handle) {
    return nvs_commit((nvs_handle_t)handle);
}
