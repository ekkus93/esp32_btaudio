#ifndef BT_STREAMING_H
#define BT_STREAMING_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Check if audio is currently streaming
 * 
 * @return true if streaming, false otherwise
 */
bool bt_is_streaming(void);

/**
 * @brief Start streaming audio to the connected Bluetooth device
 * 
 * @return ESP_OK on success, ESP_FAIL if not connected or error
 */
esp_err_t bt_start_streaming(void);

/**
 * @brief Stop streaming audio
 * 
 * @return ESP_OK on success
 */
esp_err_t bt_stop_streaming(void);

/**
 * @brief Pause audio streaming
 * 
 * @return ESP_OK on success, ESP_FAIL if not streaming
 */
esp_err_t bt_pause_streaming(void);

/**
 * @brief Resume previously paused audio streaming
 * 
 * @return ESP_OK on success, ESP_FAIL if not paused
 */
esp_err_t bt_resume_streaming(void);

/**
 * @brief Set the connection state for testing
 * 
 * @param connected true if connected, false otherwise
 */
void bt_streaming_mock_set_connected(bool connected);

#endif /* BT_STREAMING_H */
