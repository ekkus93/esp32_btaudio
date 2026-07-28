#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_DUPLEX_MAC_STR_LEN 18U
#define BT_DUPLEX_ERROR_TEXT_LEN 64U

typedef enum {
    BT_DUPLEX_MODE_DISABLED = 0,
    BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC,
    BT_DUPLEX_MODE_HFP_FULL_DUPLEX,
    BT_DUPLEX_MODE_AUTO,
    BT_DUPLEX_MODE_COUNT,
} bt_duplex_mode_t;

typedef enum {
    BT_A2DP_PROFILE_DISCONNECTED = 0,
    BT_A2DP_PROFILE_CONNECTING,
    BT_A2DP_PROFILE_CONNECTED,
    BT_A2DP_PROFILE_DISCONNECTING,
    BT_A2DP_PROFILE_STATE_COUNT,
} bt_a2dp_profile_state_t;

typedef enum {
    BT_A2DP_AUDIO_STOPPED = 0,
    BT_A2DP_AUDIO_STARTED,
    BT_A2DP_AUDIO_REMOTE_SUSPENDED,
    BT_A2DP_AUDIO_STATE_COUNT,
} bt_a2dp_audio_state_t;

typedef enum {
    BT_HFP_PROFILE_UNINITIALIZED = 0,
    BT_HFP_PROFILE_DISCONNECTED,
    BT_HFP_PROFILE_CONNECTING,
    BT_HFP_PROFILE_SLC_CONNECTED,
    BT_HFP_PROFILE_DISCONNECTING,
    BT_HFP_PROFILE_FAULTED,
    BT_HFP_PROFILE_STATE_COUNT,
} bt_hfp_profile_state_t;

typedef enum {
    BT_HFP_AUDIO_DISCONNECTED = 0,
    BT_HFP_AUDIO_CONNECTING,
    BT_HFP_AUDIO_CONNECTED_CVSD,
    BT_HFP_AUDIO_CONNECTED_MSBC,
    BT_HFP_AUDIO_DISCONNECTING,
    BT_HFP_AUDIO_FAULTED,
    BT_HFP_AUDIO_STATE_COUNT,
} bt_hfp_audio_state_t;

typedef enum {
    BT_HFP_CODEC_NONE = 0,
    BT_HFP_CODEC_CVSD,
    BT_HFP_CODEC_MSBC,
    BT_HFP_CODEC_COUNT,
} bt_hfp_codec_t;

typedef enum {
    BT_HFP_I2S_STOPPED = 0,
    BT_HFP_I2S_STARTING,
    BT_HFP_I2S_RUNNING,
    BT_HFP_I2S_STOPPING,
    BT_HFP_I2S_FAULTED,
    BT_HFP_I2S_QUARANTINED,
    BT_HFP_I2S_STATE_COUNT,
} bt_hfp_i2s_state_t;

typedef enum {
    BT_AUDIO_HEALTH_OK = 0,
    BT_AUDIO_HEALTH_DEGRADED,
    BT_AUDIO_HEALTH_FAULTED,
    BT_AUDIO_HEALTH_QUARANTINED,
    BT_AUDIO_HEALTH_COUNT,
} bt_audio_health_t;

typedef struct {
    uint64_t stale_generation_events;
    uint64_t wrong_peer_events;
    uint64_t illegal_transitions;
    uint64_t invalid_arguments;
    uint64_t recoveries;
    uint64_t incoming_frames;
    uint64_t incoming_bytes;
    uint64_t incoming_dropped_frames;
    uint64_t incoming_dropped_bytes;
    uint64_t i2s_underflows;
    uint64_t i2s_timeouts;
} bt_duplex_counters_t;

typedef struct {
    bool peer_valid;
    char peer_mac[BT_DUPLEX_MAC_STR_LEN];
    uint32_t session_generation;
    bt_duplex_mode_t requested_mode;
    bt_duplex_mode_t effective_mode;
    bt_a2dp_profile_state_t a2dp_profile_state;
    bt_a2dp_audio_state_t a2dp_audio_state;
    bt_hfp_profile_state_t hfp_profile_state;
    bt_hfp_audio_state_t hfp_audio_state;
    bt_hfp_codec_t codec;
    bt_hfp_i2s_state_t i2s_state;
    bt_audio_health_t health;
    esp_err_t last_error;
    char last_error_text[BT_DUPLEX_ERROR_TEXT_LEN];
    bt_duplex_counters_t counters;
} bt_duplex_snapshot_t;

esp_err_t bt_duplex_state_init(void);
void bt_duplex_state_deinit(void);

esp_err_t bt_duplex_session_begin(const char *peer_mac,
                                  bt_duplex_mode_t requested_mode,
                                  uint32_t *generation_out);
esp_err_t bt_duplex_get_snapshot(bt_duplex_snapshot_t *out);

/* Global HFP profile lifecycle transition used before a peer session exists
 * and after the HFP profile has been fully deinitialized. */
esp_err_t bt_duplex_set_hfp_profile_global_state(
    bt_hfp_profile_state_t state);

esp_err_t bt_duplex_set_requested_mode(uint32_t generation,
                                      const char *peer_mac,
                                      bt_duplex_mode_t mode);
esp_err_t bt_duplex_set_effective_mode(uint32_t generation,
                                      const char *peer_mac,
                                      bt_duplex_mode_t mode);
esp_err_t bt_duplex_set_a2dp_profile_state(uint32_t generation,
                                          const char *peer_mac,
                                          bt_a2dp_profile_state_t state);
esp_err_t bt_duplex_set_a2dp_audio_state(uint32_t generation,
                                        const char *peer_mac,
                                        bt_a2dp_audio_state_t state);
esp_err_t bt_duplex_set_hfp_profile_state(uint32_t generation,
                                         const char *peer_mac,
                                         bt_hfp_profile_state_t state);
esp_err_t bt_duplex_set_hfp_audio_state(uint32_t generation,
                                       const char *peer_mac,
                                       bt_hfp_audio_state_t state);
esp_err_t bt_duplex_set_i2s_state(uint32_t generation,
                                 const char *peer_mac,
                                 bt_hfp_i2s_state_t state);
esp_err_t bt_duplex_set_health(uint32_t generation,
                              const char *peer_mac,
                              bt_audio_health_t health,
                              esp_err_t error,
                              const char *error_text);
esp_err_t bt_duplex_recover(uint32_t generation, const char *peer_mac,
                            uint32_t *new_generation_out);

esp_err_t bt_duplex_record_incoming(uint32_t generation, const char *peer_mac,
                                    size_t bytes, bool accepted);
esp_err_t bt_duplex_record_i2s_underflow(uint32_t generation, const char *peer_mac);
esp_err_t bt_duplex_record_i2s_timeout(uint32_t generation, const char *peer_mac,
                                      esp_err_t error);

const char *bt_duplex_mode_to_string(bt_duplex_mode_t value);
const char *bt_a2dp_profile_state_to_string(bt_a2dp_profile_state_t value);
const char *bt_a2dp_audio_state_to_string(bt_a2dp_audio_state_t value);
const char *bt_hfp_profile_state_to_string(bt_hfp_profile_state_t value);
const char *bt_hfp_audio_state_to_string(bt_hfp_audio_state_t value);
const char *bt_hfp_codec_to_string(bt_hfp_codec_t value);
const char *bt_hfp_i2s_state_to_string(bt_hfp_i2s_state_t value);
const char *bt_audio_health_to_string(bt_audio_health_t value);

#ifdef UNIT_TEST
void bt_duplex_test_reset(void);
#endif

#ifdef __cplusplus
}
#endif
