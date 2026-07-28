#include "bt_hfp_ag_internal.h"
#include "bt_hfp_audio.h"
#include "bt_hfp_connection.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "esp_hf_ag_api.h"
#endif

static bool get_session(const char *peer_mac,
                        bt_duplex_snapshot_t *snapshot_out)
{
    if (peer_mac == NULL || snapshot_out == NULL) return false;
    return bt_duplex_get_snapshot(snapshot_out) == ESP_OK;
}

void bt_hfp_ag_handle_connection_state(const char *peer_mac,
                                       bt_hfp_ag_connection_state_t state)
{
    if (bt_hfp_ag_lock() == ESP_OK) {
        g_bt_hfp_ag.snapshot.connection_events++;
        bt_hfp_ag_remember_peer_locked(peer_mac);
        (void)bt_hfp_ag_unlock(ESP_OK);
    }

    esp_err_t tracked = bt_hfp_connection_handle_event(peer_mac, state);
    if (tracked == ESP_OK) return;
    if (tracked != ESP_ERR_NOT_FOUND) {
        bt_hfp_ag_handle_invalid_event();
        return;
    }

    bt_duplex_snapshot_t snapshot;
    if (!get_session(peer_mac, &snapshot)) return;
    bt_hfp_profile_state_t mapped;
    switch (state) {
    case BT_HFP_AG_CONNECTION_DISCONNECTED:
        mapped = BT_HFP_PROFILE_DISCONNECTED;
        break;
    case BT_HFP_AG_CONNECTION_CONNECTING:
    case BT_HFP_AG_CONNECTION_RFCOMM_CONNECTED:
        mapped = BT_HFP_PROFILE_CONNECTING;
        break;
    case BT_HFP_AG_CONNECTION_SLC_CONNECTED:
        mapped = BT_HFP_PROFILE_SLC_CONNECTED;
        break;
    case BT_HFP_AG_CONNECTION_DISCONNECTING:
        mapped = BT_HFP_PROFILE_DISCONNECTING;
        break;
    default:
        bt_hfp_ag_handle_invalid_event();
        return;
    }
    (void)bt_duplex_set_hfp_profile_state(snapshot.session_generation,
                                          peer_mac, mapped);
}

void bt_hfp_ag_handle_audio_state(const char *peer_mac,
                                  bt_hfp_ag_audio_state_t state)
{
    if (bt_hfp_ag_lock() == ESP_OK) {
        g_bt_hfp_ag.snapshot.audio_events++;
        bt_hfp_ag_remember_peer_locked(peer_mac);
        (void)bt_hfp_ag_unlock(ESP_OK);
    }
    bt_duplex_snapshot_t snapshot;
    if (!get_session(peer_mac, &snapshot)) return;
    bt_hfp_audio_state_t mapped;
    switch (state) {
    case BT_HFP_AG_AUDIO_DISCONNECTED:
        mapped = BT_HFP_AUDIO_DISCONNECTED;
        break;
    case BT_HFP_AG_AUDIO_CONNECTING:
        mapped = BT_HFP_AUDIO_CONNECTING;
        break;
    case BT_HFP_AG_AUDIO_CONNECTED_CVSD:
        mapped = BT_HFP_AUDIO_CONNECTED_CVSD;
        break;
    case BT_HFP_AG_AUDIO_CONNECTED_MSBC:
        mapped = BT_HFP_AUDIO_CONNECTED_MSBC;
        break;
    default:
        bt_hfp_ag_handle_invalid_event();
        return;
    }
    (void)bt_duplex_set_hfp_audio_state(snapshot.session_generation,
                                        peer_mac, mapped);
}

void bt_hfp_ag_handle_codec_event(const char *peer_mac,
                                  bt_hfp_ag_codec_t codec)
{
    if (bt_hfp_ag_lock() != ESP_OK) return;
    g_bt_hfp_ag.snapshot.codec_events++;
    bt_hfp_ag_remember_peer_locked(peer_mac);
    if (codec > BT_HFP_AG_CODEC_MSBC) {
        g_bt_hfp_ag.snapshot.invalid_events++;
    } else {
        g_bt_hfp_ag.snapshot.last_codec_event = codec;
    }
    (void)bt_hfp_ag_unlock(ESP_OK);
}

void bt_hfp_ag_handle_volume_event(const char *peer_mac,
                                   bool microphone,
                                   int volume)
{
    if (bt_hfp_ag_lock() != ESP_OK) return;
    g_bt_hfp_ag.snapshot.volume_events++;
    bt_hfp_ag_remember_peer_locked(peer_mac);
    if (volume < 0 || volume > 15) {
        g_bt_hfp_ag.snapshot.invalid_events++;
    } else if (microphone) {
        g_bt_hfp_ag.snapshot.microphone_volume = volume;
    } else {
        g_bt_hfp_ag.snapshot.speaker_volume = volume;
    }
    (void)bt_hfp_ag_unlock(ESP_OK);
}

void bt_hfp_ag_handle_unknown_at(const char *peer_mac)
{
    if (bt_hfp_ag_lock() == ESP_OK) {
        g_bt_hfp_ag.snapshot.unknown_at_events++;
        bt_hfp_ag_remember_peer_locked(peer_mac);
        (void)bt_hfp_ag_unlock(ESP_OK);
    }
    esp_err_t err = bt_hfp_ag_platform_unknown_at_error(peer_mac);
    if (err != ESP_OK && bt_hfp_ag_lock() == ESP_OK) {
        g_bt_hfp_ag.snapshot.response_failures++;
        g_bt_hfp_ag.snapshot.last_error = err;
        (void)bt_hfp_ag_unlock(ESP_OK);
    }
}

void bt_hfp_ag_handle_unhandled_event(void)
{
    if (bt_hfp_ag_lock() != ESP_OK) return;
    g_bt_hfp_ag.snapshot.unhandled_events++;
    (void)bt_hfp_ag_unlock(ESP_OK);
}

void bt_hfp_ag_handle_invalid_event(void)
{
    if (bt_hfp_ag_lock() != ESP_OK) return;
    g_bt_hfp_ag.snapshot.invalid_events++;
    (void)bt_hfp_ag_unlock(ESP_OK);
}

#ifdef ESP_PLATFORM
static void bda_to_string(const esp_bd_addr_t bda,
                          char out[BT_HFP_AG_MAC_STR_LEN])
{
    (void)snprintf(out, BT_HFP_AG_MAC_STR_LEN,
                   "%02X:%02X:%02X:%02X:%02X:%02X",
                   bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

static esp_err_t unknown_at_error_production(const char *peer_mac)
{
    unsigned values[6];
    if (peer_mac == NULL ||
        sscanf(peer_mac, "%02x:%02x:%02x:%02x:%02x:%02x",
               &values[0], &values[1], &values[2], &values[3],
               &values[4], &values[5]) != 6) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_bd_addr_t bda;
    for (size_t i = 0; i < 6; ++i) bda[i] = (uint8_t)values[i];
    return esp_hf_ag_unknown_at_send(bda, NULL);
}

static void bind_audio_callback_state(const char *peer,
                                      const esp_hf_cb_param_t *param)
{
    bt_duplex_snapshot_t snapshot;
    if (bt_duplex_get_snapshot(&snapshot) != ESP_OK) return;

    esp_err_t route = bt_hfp_audio_apply_duplex_state(
        &snapshot, peer, param->audio_stat.sync_conn_handle,
        param->audio_stat.preferred_frame_size);
    bool connected_event =
        param->audio_stat.state == ESP_HF_AUDIO_STATE_CONNECTED ||
        param->audio_stat.state == ESP_HF_AUDIO_STATE_CONNECTED_MSBC;
    bool same_peer = snapshot.peer_valid &&
        strcasecmp(snapshot.peer_mac, peer) == 0;
    if (route != ESP_OK && connected_event && same_peer) {
        const char *reason = route == ESP_ERR_NOT_SUPPORTED
            ? "mSBC unsupported during CVSD phase"
            : "HFP audio callback route not ready";
        (void)bt_duplex_set_health(snapshot.session_generation, peer,
                                   BT_AUDIO_HEALTH_DEGRADED, route, reason);
    }
}

static void handle_production_audio_event(const char *peer,
                                          const esp_hf_cb_param_t *param)
{
    bt_hfp_ag_audio_state_t state =
        (bt_hfp_ag_audio_state_t)param->audio_stat.state;
    esp_err_t tracked = bt_hfp_audio_control_handle_event(
        peer, state, param->audio_stat.sync_conn_handle,
        param->audio_stat.preferred_frame_size);
    if (tracked == ESP_OK) {
        if (bt_hfp_ag_lock() == ESP_OK) {
            g_bt_hfp_ag.snapshot.audio_events++;
            bt_hfp_ag_remember_peer_locked(peer);
            (void)bt_hfp_ag_unlock(ESP_OK);
        }
        return;
    }
    if (tracked != ESP_ERR_NOT_FOUND) {
        bt_hfp_ag_handle_invalid_event();
        return;
    }

    bt_hfp_ag_handle_audio_state(peer, state);
    bind_audio_callback_state(peer, param);
}

static void production_event_callback(esp_hf_cb_event_t event,
                                      esp_hf_cb_param_t *param)
{
    if (param == NULL) {
        bt_hfp_ag_handle_invalid_event();
        return;
    }
    char peer[BT_HFP_AG_MAC_STR_LEN];
    switch (event) {
    case ESP_HF_PROF_STATE_EVT:
        switch (param->prof_stat.state) {
        case ESP_HF_INIT_SUCCESS:
            bt_hfp_ag_handle_profile_result(BT_HFP_AG_PROFILE_INIT_SUCCESS);
            break;
        case ESP_HF_INIT_ALREADY:
            bt_hfp_ag_handle_profile_result(BT_HFP_AG_PROFILE_INIT_ALREADY);
            break;
        case ESP_HF_INIT_FAIL:
            bt_hfp_ag_handle_profile_result(BT_HFP_AG_PROFILE_INIT_FAILED);
            break;
        case ESP_HF_DEINIT_SUCCESS:
            bt_hfp_ag_handle_profile_result(
                BT_HFP_AG_PROFILE_DEINIT_SUCCESS);
            break;
        case ESP_HF_DEINIT_ALREADY:
            bt_hfp_ag_handle_profile_result(
                BT_HFP_AG_PROFILE_DEINIT_ALREADY);
            break;
        case ESP_HF_DEINIT_FAIL:
            bt_hfp_ag_handle_profile_result(BT_HFP_AG_PROFILE_DEINIT_FAILED);
            break;
        default:
            bt_hfp_ag_handle_invalid_event();
            break;
        }
        break;
    case ESP_HF_CONNECTION_STATE_EVT:
        bda_to_string(param->conn_stat.remote_bda, peer);
        bt_hfp_ag_handle_connection_state(
            peer, (bt_hfp_ag_connection_state_t)param->conn_stat.state);
        break;
    case ESP_HF_AUDIO_STATE_EVT:
        bda_to_string(param->audio_stat.remote_addr, peer);
        handle_production_audio_event(peer, param);
        break;
    case ESP_HF_BCS_RESPONSE_EVT:
        bda_to_string(param->bcs_rep.remote_addr, peer);
        bt_hfp_ag_handle_codec_event(
            peer, param->bcs_rep.mode == ESP_HF_WBS_YES
                      ? BT_HFP_AG_CODEC_MSBC : BT_HFP_AG_CODEC_CVSD);
        break;
#if CONFIG_BT_HFP_WBS_ENABLE
    case ESP_HF_WBS_RESPONSE_EVT:
        bda_to_string(param->wbs_rep.remote_addr, peer);
        bt_hfp_ag_handle_codec_event(
            peer, param->wbs_rep.codec == ESP_HF_WBS_YES
                      ? BT_HFP_AG_CODEC_MSBC : BT_HFP_AG_CODEC_CVSD);
        break;
#endif
    case ESP_HF_VOLUME_CONTROL_EVT:
        bda_to_string(param->volume_control.remote_addr, peer);
        bt_hfp_ag_handle_volume_event(
            peer, param->volume_control.type == ESP_HF_VOLUME_TYPE_MIC,
            param->volume_control.volume);
        break;
    case ESP_HF_UNAT_RESPONSE_EVT:
        bda_to_string(param->unat_rep.remote_addr, peer);
        bt_hfp_ag_handle_unknown_at(peer);
        break;
    default:
        bt_hfp_ag_handle_unhandled_event();
        break;
    }
}

esp_err_t bt_hfp_ag_register_platform_callback(void)
{
    return esp_hf_ag_register_callback(production_event_callback);
}
#endif

esp_err_t bt_hfp_ag_platform_unknown_at_error(const char *peer_mac)
{
#ifdef UNIT_TEST
    if (g_bt_hfp_ag.test_ops_set &&
        g_bt_hfp_ag.test_ops.unknown_at_error != NULL) {
        return g_bt_hfp_ag.test_ops.unknown_at_error(peer_mac);
    }
#endif
#ifdef ESP_PLATFORM
    return unknown_at_error_production(peer_mac);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
