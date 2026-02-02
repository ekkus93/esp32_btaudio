#ifndef BT_STREAMING_H_
#define BT_STREAMING_H_

#include <stdbool.h>
#include "esp_bt_defs.h"
#include "esp_err.h"
#include "bt_source.h"  // Include bt_source.h to use its types

#ifdef __cplusplus
extern "C" {
#endif

// Use bt_stream_state_t from bt_source.h
// No need to redefine bt_streaming_state_t

/**
 * For test purposes - simulate a connection
 */
void bt_streaming_mock_set_connected(bool connected);

/**
 * Check if streaming is active
 */
bool bt_is_streaming(void);

/**
 * Start streaming audio
 */
esp_err_t bt_start_streaming(void);

/**
 * Stop streaming audio
 */
esp_err_t bt_stop_streaming(void);

/**
 * Pause audio streaming
 */
esp_err_t bt_pause_streaming(void);

/**
 * Resume audio streaming
 */
esp_err_t bt_resume_streaming(void);

/**
 * Get current streaming state
 * This function should return bt_stream_state_t from bt_source.h
 */
#define bt_get_streaming_state bt_get_stream_state

#ifdef __cplusplus
}
#endif

#endif /* BT_STREAMING_H_ */
