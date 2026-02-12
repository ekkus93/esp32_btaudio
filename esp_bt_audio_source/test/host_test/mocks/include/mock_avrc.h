/**
 * @file mock_avrc.h
 * @brief Mock interface for AVRC (Audio/Video Remote Control) profile
 */

#pragma once

#include <stdbool.h>
#include "esp_avrc_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reset mock state to defaults
 */
void mock_avrc_reset(void);

/**
 * @brief Set result for esp_avrc_ct_init()
 * @param result The error code to return (ESP_OK or ESP_FAIL)
 */
void mock_avrc_set_init_result(esp_err_t result);

/**
 * @brief Set result for esp_avrc_ct_register_callback()
 * @param result The error code to return (ESP_OK or ESP_FAIL)
 */
void mock_avrc_set_callback_result(esp_err_t result);

/**
 * @brief Check if esp_avrc_ct_init() was called
 * @return true if called, false otherwise
 */
bool mock_avrc_was_init_called(void);

/**
 * @brief Check if esp_avrc_ct_deinit() was called
 * @return true if called, false otherwise
 */
bool mock_avrc_was_deinit_called(void);

/**
 * @brief Get the registered callback function
 * @return The callback registered via esp_avrc_ct_register_callback(), or NULL
 */
esp_avrc_ct_cb_t mock_avrc_get_registered_callback(void);

#ifdef __cplusplus
}
#endif
