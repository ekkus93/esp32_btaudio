/* Stub nvs_flash.h for host tests */
#ifndef STUB_NVS_FLASH_H
#define STUB_NVS_FLASH_H
#include "esp_err.h"
#define ESP_ERR_NVS_NO_FREE_PAGES    (-109)
#define ESP_ERR_NVS_NEW_VERSION_FOUND (-110)
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
#endif
