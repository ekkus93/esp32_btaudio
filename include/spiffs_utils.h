#ifndef SPIFFS_UTILS_H
#define SPIFFS_UTILS_H

#include "esp_err.h"
#include <stdbool.h>  // Ensure this include for bool is present

#define MAX_SPIFFS_PATH 32
#define SPIFFS_BASE_PATH "/spiffs"

esp_err_t init_spiffs(void);
esp_err_t mount_spiffs_fs(void);
bool is_spiffs_mounted(void);
void list_spiffs_files(void);

#endif // SPIFFS_UTILS_H