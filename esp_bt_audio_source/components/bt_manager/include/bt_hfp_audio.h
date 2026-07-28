#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bt_duplex_state.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_HFP_AUDIO_INVALID_SYNC_HANDLE UINT16_MAX
#define BT_HFP_AUDIO_CALLBACK_BUDGET_US 2000U

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
    uint32_t callback_last_us;
    uint32_t callback_max_us;
    uint32_t active_callbacks;
    esp_err_t last_error;
} bt_hfp_audio_snapshot_t;

/* Called by the HFP AG lifecycle only after profile initialization has been
 * confirmed. ESP-IDF v5.5.1's non-legacy HCI API registers one incoming-audio
 * callback; outgoing audio is sent explicitly through esp_hf_ag_audio_data_send
 * and is intentionally outside FD-09. */
esp_err_t bt_hfp_audio_register_callback(void);

/* Apply a callback-safe fast-state binding derived from one authoritative
 * duplex snapshot. Connected CVSD is accepted only when the same peer/session
 * owns an already-running I2S output. All other states disable acceptance. */
esp_err_t bt_hfp_audio_apply_duplex_state(
    const bt_duplex_snapshot_t *snapshot,
    const char *event_peer_mac,
    uint16_t sync_conn_handle,
    uint16_t preferred_frame_size);

/* Stop accepting new audio before profile/audio teardown. This operation is
 * nonblocking and safe even if one bounded callback is already in flight. */
void bt_hfp_audio_profile_stopping(void);

/* Cleanup is legal only after Bluedroid callback delivery is impossible. */
esp_err_t bt_hfp_audio_cleanup_after_stack_shutdown(void);

esp_err_t bt_hfp_audio_get_snapshot(bt_hfp_audio_snapshot_t *out);

#ifdef UNIT_TEST
typedef struct {
    esp_err_t (*register_callback)(void);
    int64_t (*now_us)(void);
} bt_hfp_audio_platform_ops_t;

esp_err_t bt_hfp_audio_test_set_platform_ops(
    const bt_hfp_audio_platform_ops_t *ops);
void bt_hfp_audio_test_reset(void);
void bt_hfp_audio_test_handle_incoming(uint16_t sync_conn_handle,
                                       const uint8_t *data,
                                       size_t data_len,
                                       size_t buffer_capacity,
                                       bool bad_frame);
#endif

#ifdef __cplusplus
}
#endif
