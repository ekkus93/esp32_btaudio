#ifndef BT_APP_NAME_H
#define BT_APP_NAME_H

#include <esp_err.h>

esp_err_t bluetooth_set_device_name(const char *name);
esp_err_t bluetooth_get_device_name(char *name, size_t max_len);

#endif // BT_APP_NAME_H
