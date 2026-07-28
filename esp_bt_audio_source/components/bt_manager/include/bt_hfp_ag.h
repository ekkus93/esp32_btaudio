#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_HFP_AG_MAC_STR_LEN 18U
#define BT_HFP_AG_DEFAULT_LIFECYCLE_TIMEOUT_MS 5000U

typedef enum {
    BT_HFP_AG_LIFECYCLE_UNINITIALIZED = 0,
    BT_HFP_AG_LIFECYCLE_INIT_PENDING,
    BT_HFP_AG_LIFECYCLE_READY,
    BT_HFP_AG_LIFECYCLE_DEINIT_PENDING,
    BT_HFP_AG_LIFECYCLE_FAULTED,
} bt_hfp_ag_lifecycle_t;

typedef enum {
    BT_HFP_AG_PROFILE_INIT_SUCCESS = 0,
    BT_HFP_AG_PROFILE_INIT_ALREADY,
    BT_HFP_AG_PROFILE_INIT_FAILED,
    BT_HFP_AG_PROFILE_DEINIT_SUCCESS,
    BT_HFP_AG_PROFILE_DEINIT_ALREADY,
    BT_HFP_AG_PROFILE_DEINIT_FAILED,
} bt_hfp_ag_profile_result_t;

typedef enum {
    BT_HFP_AG_CONNECTION_DISCONNECTED = 0,
    BT_HFP_AG_CONNECTION_CONNECTING,
    BT_HFP_AG_CONNECTION_RFCOMM_CONNECTED,
    BT_HFP_AG_CONNECTION_SLC_CONNECTED,
    BT_HFP_AG_CONNECTION_DISCONNECTING,
} bt_hfp_ag_connection_state_t;

typedef enum {
    BT_HFP_AG_AUDIO_DISCONNECTED = 0,
    BT_HFP_AG_AUDIO_CONNECTING,
    BT_HFP_AG_AUDIO_CONNECTED_CVSD,
    BT_HFP_AG_AUDIO_CONNECTED_MSBC,
} bt_hfp_ag_audio_state_t;

typedef enum {
    BT_HFP_AG_CODEC_NONE = 0,
    BT_HFP_AG_CODEC_CVSD,
    BT_HFP_AG_CODEC_MSBC,
} bt_hfp_ag_codec_t;

typedef struct {
    bt_hfp_ag_lifecycle_t lifecycle;
    bool callback_registered;
    bool profile_init_request_accepted;
    bool profile_ready;
    esp_err_t last_error;
    bt_hfp_ag_codec_t last_codec_event;
    char last_peer_mac[BT_HFP_AG_MAC_STR_LEN];
    int speaker_volume;
    int microphone_volume;
    uint64_t profile_events;
    uint64_t connection_events;
    uint64_t audio_events;
    uint64_t codec_events;
    uint64_t volume_events;
    uint64_t unknown_at_events;
    uint64_t unhandled_events;
    uint64_t invalid_events;
    uint64_t response_failures;
    uint64_t init_timeouts;
    uint64_t deinit_timeouts;
} bt_hfp_ag_snapshot_t;

esp_err_t bt_hfp_ag_profile_init(uint32_t timeout_ms);
esp_err_t bt_hfp_ag_profile_deinit(uint32_t timeout_ms);
void bt_hfp_ag_force_cleanup_after_stack_shutdown(void);
esp_err_t bt_hfp_ag_get_snapshot(bt_hfp_ag_snapshot_t *out);

void bt_hfp_ag_handle_profile_result(bt_hfp_ag_profile_result_t result);
void bt_hfp_ag_handle_connection_state(const char *peer_mac,
                                       bt_hfp_ag_connection_state_t state);
void bt_hfp_ag_handle_audio_state(const char *peer_mac,
                                  bt_hfp_ag_audio_state_t state);
void bt_hfp_ag_handle_codec_event(const char *peer_mac,
                                  bt_hfp_ag_codec_t codec);
void bt_hfp_ag_handle_volume_event(const char *peer_mac,
                                   bool microphone,
                                   int volume);
void bt_hfp_ag_handle_unknown_at(const char *peer_mac);
void bt_hfp_ag_handle_unhandled_event(void);
void bt_hfp_ag_handle_invalid_event(void);

#ifdef UNIT_TEST
typedef struct {
    esp_err_t (*register_callback)(void);
    esp_err_t (*profile_init)(void);
    esp_err_t (*profile_deinit)(void);
    esp_err_t (*unknown_at_error)(const char *peer_mac);
} bt_hfp_ag_platform_ops_t;

esp_err_t bt_hfp_ag_test_set_platform_ops(const bt_hfp_ag_platform_ops_t *ops);
void bt_hfp_ag_test_reset_platform_ops(void);
#endif

#ifdef __cplusplus
}
#endif
