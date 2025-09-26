#ifndef ESP_ERR_H
#define ESP_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical minimal esp_err_t and error macros for host tests.
 * Provide guarded definitions so multiple mock headers can include
 * this file without causing typedef/macro redefinition problems.
 */
#ifndef __ESP_ERR_T_DEFINED
typedef int esp_err_t;
#define __ESP_ERR_T_DEFINED
#endif

#ifndef ESP_OK
#define ESP_OK 0
#endif

#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG (-22)
#endif

#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND (-127)
#endif

#ifdef __cplusplus
}
#endif

#endif // ESP_ERR_H
