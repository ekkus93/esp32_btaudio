/* Stub nvs.h for host tests */
#ifndef STUB_NVS_H
#define STUB_NVS_H
#include <stdint.h>
typedef int nvs_handle_t;
#define NVS_READONLY  0
#define NVS_READWRITE 1
#include "esp_err.h"
esp_err_t nvs_open(const char *namespace, int flags, nvs_handle_t *out);
esp_err_t nvs_get_i32(nvs_handle_t h, const char *key, int32_t *out);
esp_err_t nvs_set_i32(nvs_handle_t h, const char *key, int32_t val);
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *out);
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t val);
esp_err_t nvs_commit(nvs_handle_t h);
void nvs_close(nvs_handle_t h);
void mock_nvs_set_open_err(esp_err_t err);
void mock_nvs_set_set_err(esp_err_t err);
void mock_nvs_set_commit_err(esp_err_t err);
#endif
