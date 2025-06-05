/**
 * @file test_config.h
 * @brief Configuration and declarations for the BT test suite
 */

#pragma once

#include "bt_source.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Type definition for BT interface
 */
typedef struct {
    esp_err_t (*init)(void);
    esp_err_t (*scan_start)(void);
    esp_err_t (*scan_stop)(void);
    esp_err_t (*connect)(const char* addr);
    esp_err_t (*reset)(void);
} bt_interface_t;

/**
 * Get BT implementation for tests
 */
bt_interface_t* get_bt_implementation(void);

#ifdef __cplusplus
}
#endif
