/**
 * @file bt_events_a2dp.h
 * @brief Bluetooth A2DP (Advanced Audio Distribution Profile) event handlers
 *
 * Handles A2DP source events including connection state changes, audio state
 * changes, and provides the data callback for streaming PCM audio.
 */

#ifndef BT_EVENTS_A2DP_H
#define BT_EVENTS_A2DP_H

#include "esp_a2dp_api.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// Expose callbacks for both ESP_PLATFORM and UNIT_TEST
#if defined(ESP_PLATFORM) || defined(UNIT_TEST)
/**
 * @brief A2DP callback for audio connection and streaming events
 *
 * Handles:
 * - ESP_A2D_CONNECTION_STATE_EVT: Connection state changes
 * - ESP_A2D_AUDIO_STATE_EVT: Audio streaming state changes
 *
 * @param event A2DP event type
 * @param param Event parameters (event-specific data)
 */
void bt_events_a2dp_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);

/**
 * @brief A2DP data callback for streaming audio
 *
 * Called by ESP-IDF A2DP implementation when audio data is needed.
 * Reads PCM samples from audio processor and fills the provided buffer.
 *
 * @param buf Buffer to fill with PCM audio data
 * @param len Size of buffer in bytes
 * @return Number of bytes written to buffer (0 on error)
 */
int32_t bt_events_a2dp_data_callback(uint8_t *buf, int32_t len);

/**
 * @brief Handle A2DP connection state change event
 *
 * Validates the event-owned ESP-IDF connection handle before mutating base
 * state, invokes user callbacks, forwards to the connection manager, and
 * triggers auto-start if enabled.
 *
 * @param param A2DP callback parameters containing connection state
 */
void bt_events_handle_a2dp_connection(const esp_a2d_cb_param_t *param);

/**
 * @brief Handle A2DP audio streaming state change event
 *
 * Validates the event-owned ESP-IDF connection handle before mutating base
 * state and forwarding to the connection manager.
 *
 * @param param A2DP callback parameters containing audio state
 */
void bt_events_handle_a2dp_audio(const esp_a2d_cb_param_t *param);

/**
 * @brief Clear the complete A2DP profile-binding state under the manager mutex
 *
 * The Bluetooth manager context mutex must already exist. This function
 * acquires that mutex internally, clears the peer/handle/lifecycle/generation
 * identity and all binding diagnostics, and then releases the mutex. If the
 * lock cannot be acquired, the exact lock error is returned and the binding is
 * left unchanged. Do not call this function after the manager mutex is deleted.
 *
 * @return ESP_OK after a synchronized reset, otherwise the exact lock error
 */
esp_err_t bt_events_a2dp_reset_binding(void);

#ifdef UNIT_TEST
#define BT_EVENTS_A2DP_TEST_MAC_STR_LEN 18U
typedef struct {
    bool valid;
    char peer_mac[BT_EVENTS_A2DP_TEST_MAC_STR_LEN];
    esp_a2d_conn_hdl_t conn_handle;
    uint32_t lifecycle_serial;
    uint32_t last_duplex_generation;
    uint64_t missing_binding_rejections;
    uint64_t wrong_peer_rejections;
    uint64_t stale_handle_rejections;
    uint64_t generation_sync_failures;
    uint64_t late_terminal_events_ignored;
} bt_events_a2dp_binding_snapshot_t;

esp_err_t bt_events_a2dp_test_get_binding(
    bt_events_a2dp_binding_snapshot_t *out);
esp_err_t bt_events_a2dp_test_reset_binding(void);
#endif

#else
// Stub implementations for non-ESP builds
static inline void bt_events_a2dp_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
    (void)event; (void)param;
}
static inline int32_t bt_events_a2dp_data_callback(uint8_t *buf, int32_t len) {
    (void)buf; (void)len; return 0;
}
static inline esp_err_t bt_events_a2dp_reset_binding(void) { return ESP_OK; }
#endif // defined(ESP_PLATFORM) || defined(UNIT_TEST)

#endif // BT_EVENTS_A2DP_H
