#ifndef SPIFFS_UTILS_H
#define SPIFFS_UTILS_H

#include <esp_err.h>

esp_err_t init_spiffs(void);
void list_spiffs_files(void);
void unmount_spiffs(void);

#endif // SPIFFS_UTILS_H