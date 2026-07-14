/* Stub esp_err.h for host tests */
#ifndef STUB_ESP_ERR_H
#define STUB_ESP_ERR_H
#include <stdint.h>
typedef int32_t esp_err_t;
#define ESP_OK (0)
#define ESP_ERR_NO_MEM (-1)
#define ESP_ERR_TIMEOUT (-2)
#define ESP_ERR_INVALID_ARG (-3)
#define ESP_ERR_INVALID_STATE (-4)
#define ESP_ERR_INVALID_SIZE (-5)
const char *esp_err_to_name(esp_err_t err);
#endif
