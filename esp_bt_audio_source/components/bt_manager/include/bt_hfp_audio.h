#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bt_duplex_state.h"
#include "bt_hfp_ag.h"
#include "esp_bt.h"
#include "esp_err.h"
#include "esp_gap_bt_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_HFP_AUDIO_INVALID_SYNC_HANDLE UINT16_MAX
#define BT_HFP_AUDIO_CALLBACK_BUDGET_US 2000U
#ifndef BT_HFP_AUDIO_REQUEST_TIMEOUT_MS
#define BT_HFP_AUDIO_REQUEST_TIMEOUT_MS 1000U
#endif
#ifndef BT_HFP_AUDIO_EVENT_TIMEOUT_MS
#define BT_HFP_AUDIO_EVENT_TIMEOUT_MS 10000U
#endif

typedef enum {
    BT_HFP_AUDIO_OPERATION_NONE = 0,
    BT_HFP_AUDIO_OPERATION_START,
    BT_HFP_AUDIO_OPERATION_STOP,
} bt_hfp_audio_operation_type_t;

typedef enum {
    BT_HFP_AUDIO_OPERATION_IDLE = 0,
    BT_HFP_AUDIO_OPERATION_PREPARING,
    BT_HFP_AUDIO_OPERATION_QUEUED,
    BT_HFP_AUDIO_OPERATION_REQUEST_SENT,
    BT_HFP_AUDIO_OPERATION_WAITING_EVENT,
    BT_HFP_AUDIO_OPERATION_CONFIRMED,
    BT_HFP_AUDIO_OPERATION_REJECTED,
    BT_HFP_AUDIO_OPERATION_TIMED_OUT,
    BT_HFP_AUDIO_OPERATION_FAULTED,
} bt_hfp_audio_operation_state_t;

typedef struct {
    bool initialized;
    bool callback_registered;
    bool accepting_incoming;
    uint32_t generation;
    uint16_t sync_conn_handle;
    uint16_t preferred_frame_size;
    bt_hfp_codec_t codec;
    char peer_mac[BT_DUPLEX_MAC_STR_LEN];
    uint64_t registration_failures;
    uint64_t activation_failures;
    uint64_t incoming_callbacks;
    uint64_t accepted_frames;
    uint64_t accepted_bytes;
    uint64_t dropped_frames;
    uint64_t dropped_bytes;
    uint64_t invalid_frames;
    uint64_t invalid_bytes;
    uint64_t inactive_frames;
    uint64_t inactive_bytes;
    uint64_t stale_handle_frames;
    uint64_t stale_handle_bytes;
    uint64_t bad_frames;
    uint64_t bad_bytes;
    uint64_t unsupported_codec_frames;
    uint64_t unsupported_codec_bytes;
    uint64_t ring_rejected_frames;
    uint64_t ring_rejected_bytes;
    uint64_t callback_over_budget;
    /* Process-lifetime count of callbacks rejected because another incoming
     * callback was already active. The rejected callback performs no frame or
     * byte counter updates, so all 64-bit callback counters remain single
     * writer and cannot lose increments. */
    uint32_t callback_overlap_rejections;
    uint32_t callback_last_us;
    uint32_t callback_max_us;
    uint32_t active_callbacks;
    esp_err_t last_error;
} bt_hfp_audio_snapshot_t;

typedef struct {
    bool initialized;
    bool api_active;
    bool pending;
    bool lower_request_accepted;
    bt_hfp_audio_operation_type_t type;
    bt_hfp_audio_operation_state_t state;
    uint32_t serial;
    uint32_t generation;
    char peer_mac[BT_DUPLEX_MAC_STR_LEN];
    esp_err_t immediate_result;
    esp_err_t completion_result;
    esp_err_t cleanup_result;
    esp_err_t last_error;
    uint64_t start_calls;
    uint64_t stop_calls;
    uint64_t successful_starts;
    uint64_t successful_stops;
    uint64_t start_failures;
    uint64_t stop_failures;
    uint64_t dispatch_failures;
    uint64_t immediate_failures;
    uint64_t request_timeouts;
    uint64_t event_timeouts;
    uint64_t stale_events;
    uint64_t wrong_peer_events;
    uint64_t unexpected_connected_events;
    uint64_t rollback_attempts;
    uint64_t rollback_failures;
    uint64_t cleanup_disconnect_requests;
    uint64_t cleanup_disconnect_failures;
    uint64_t i2s_start_failures;
    uint64_t i2s_stop_failures;
    uint64_t health_report_failures;
    esp_err_t last_health_report_error;
} bt_hfp_audio_control_snapshot_t;

/* Called by the HFP AG lifecycle only after profile initialization has been
 * confirmed. ESP-IDF v5.5.1's non-legacy HCI API registers one incoming-audio
 * callback; outgoing audio is sent explicitly through esp_hf_ag_audio_data_send
 * and is intentionally outside FD-09/FD-10. */
esp_err_t bt_hfp_audio_register_callback(void);

/* Allocate the bounded FD-10 control resources after HFP profile and incoming
 * callback initialization have both been confirmed. */
esp_err_t bt_hfp_audio_control_init(void);

/* Apply a callback-safe fast-state binding derived from one authoritative
 * duplex snapshot. Connected CVSD is accepted only when the same peer/session
 * owns an already-running I2S output. All other states disable acceptance. */
esp_err_t bt_hfp_audio_apply_duplex_state(
    const bt_duplex_snapshot_t *snapshot,
    const char *event_peer_mac,
    uint16_t sync_conn_handle,
    uint16_t preferred_frame_size);

/* Start/stop are synchronous bounded orchestration APIs. ESP_OK means the
 * lower-layer request and confirming HFP audio event both completed and the
 * I2S lifecycle reached the required terminal state. */
esp_err_t bt_hfp_audio_start(void);
esp_err_t bt_hfp_audio_stop(void);

/* Consume an HFP audio-state event when it belongs to an FD-10 operation.
 * ESP_ERR_NOT_FOUND asks the general AG event mapper to handle an untracked
 * event. */
esp_err_t bt_hfp_audio_control_handle_event(
    const char *peer_mac,
    bt_hfp_ag_audio_state_t state,
    uint16_t sync_conn_handle,
    uint16_t preferred_frame_size);

esp_err_t bt_hfp_audio_control_get_snapshot(
    bt_hfp_audio_control_snapshot_t *out);

/* Stop accepting new audio before profile/audio teardown. This operation is
 * nonblocking and safe even if one bounded callback is already in flight. */
void bt_hfp_audio_profile_stopping(void);
void bt_hfp_audio_control_profile_stopping(void);

/* Cleanup is legal only after Bluedroid callback delivery is impossible. */
esp_err_t bt_hfp_audio_cleanup_after_stack_shutdown(void);
esp_err_t bt_hfp_audio_control_cleanup_after_stack_shutdown(void);

esp_err_t bt_hfp_audio_get_snapshot(bt_hfp_audio_snapshot_t *out);

#ifdef UNIT_TEST
typedef struct {
    esp_err_t (*register_callback)(void);
    int64_t (*now_us)(void);
} bt_hfp_audio_platform_ops_t;

typedef struct {
    esp_err_t (*audio_connect)(esp_bd_addr_t remote_bda);
    esp_err_t (*audio_disconnect)(esp_bd_addr_t remote_bda);
    esp_err_t (*set_health)(uint32_t generation,
                            const char *peer_mac,
                            bt_audio_health_t health,
                            esp_err_t error,
                            const char *text);
} bt_hfp_audio_control_platform_ops_t;

esp_err_t bt_hfp_audio_test_set_platform_ops(
    const bt_hfp_audio_platform_ops_t *ops);
void bt_hfp_audio_test_reset(void);
void bt_hfp_audio_test_handle_incoming(uint16_t sync_conn_handle,
                                       const uint8_t *data,
                                       size_t data_len,
                                       size_t buffer_capacity,
                                       bool bad_frame);
void bt_hfp_audio_test_record_callback_duration(uint32_t duration_us);

esp_err_t bt_hfp_audio_control_test_set_platform_ops(
    const bt_hfp_audio_control_platform_ops_t *ops);
void bt_hfp_audio_control_test_reset(void);
#endif

#ifdef __cplusplus
}
#endif
