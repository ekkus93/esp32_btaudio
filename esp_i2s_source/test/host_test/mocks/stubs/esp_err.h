/* Stub esp_err.h for radio lifecycle host tests */
#ifndef STUB_ESP_ERR_H
#define STUB_ESP_ERR_H
#include <stdint.h>
typedef int32_t esp_err_t;
#define ESP_OK            (0)
#define ESP_ERR_NO_MEM    (-1)
#define ESP_ERR_TIMEOUT   (-2)
#define ESP_ERR_INVALID_STATE (-3)
#define ESP_ERR_INVALID_ARG (-4)
#define ESP_ERR_INVALID_SIZE (-5)
/* NVS-specific errors (matching ESP-IDF error codes) */
#define ESP_ERR_NVS_NO_FREE_KEYS    (-101)
#define ESP_ERR_NVS_NOT_FOUND       (-102)
#define ESP_ERR_NVS_VERSION         (-103)
#define ESP_ERR_NVS_WRONG_TYPE      (-104)
#define ESP_ERR_NVS_NO_ROOM         (-105)
#define ESP_ERR_NVS_KEY_BUSY        (-106)
#define ESP_ERR_NVS_CORRUPT_DATA    (-107)
#define ESP_ERR_NVS_ALREADY_OPEN    (-108)
const char *esp_err_to_name(esp_err_t err);
/* Host stand-in: real ESP-IDF aborts the process on failure. Host stub
 * functions here are set up to always succeed, so just discard the result. */
#define ESP_ERROR_CHECK(x) do { (void)(x); } while (0)
#endif
