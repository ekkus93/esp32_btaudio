#include "bt_hfp_audio_control_internal.h"

static void record_wrong_peer_event(void)
{
    if (control_lock() == ESP_OK) {
        s_control.snapshot.wrong_peer_events++;
        (void)control_unlock(ESP_OK);
    }
}

static void reject_unexpected_connected_event(
    const char *peer_mac, const bt_duplex_snapshot_t *duplex)
{
    bt_hfp_audio_profile_stopping();
    if (control_lock() == ESP_OK) {
        s_control.snapshot.unexpected_connected_events++;
        (void)control_unlock(ESP_OK);
    }

    if (duplex != NULL && duplex->peer_valid &&
        same_peer(peer_mac, duplex->peer_mac)) {
        set_health(duplex->session_generation, duplex->peer_mac,
                   BT_AUDIO_HEALTH_DEGRADED, ESP_ERR_INVALID_STATE,
                   "Unexpected HFP audio connection was rejected");
        dispatch_remote_cleanup(duplex->session_generation,
                                duplex->peer_mac, true, true);
    } else {
        record_wrong_peer_event();
        /* No authoritative peer state is mutated for a foreign connection.
         * The lower link is still explicitly rejected. */
        dispatch_remote_cleanup(0U, peer_mac, true, false);
    }
}

esp_err_t bt_hfp_audio_control_handle_event(
    const char *peer_mac,
    bt_hfp_ag_audio_state_t state,
    uint16_t sync_conn_handle,
    uint16_t preferred_frame_size)
{
    if (peer_mac == NULL || state > BT_HFP_AG_AUDIO_CONNECTED_MSBC) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_control.initialized) return ESP_ERR_NOT_FOUND;

    bt_hfp_audio_control_snapshot_t operation;
    esp_err_t err = bt_hfp_audio_control_get_snapshot(&operation);
    if (err != ESP_OK) return err;

    bt_duplex_snapshot_t duplex;
    const esp_err_t duplex_err = bt_duplex_get_snapshot(&duplex);

    if (operation.type == BT_HFP_AUDIO_OPERATION_NONE) {
        if (state == BT_HFP_AG_AUDIO_CONNECTED_CVSD ||
            state == BT_HFP_AG_AUDIO_CONNECTED_MSBC) {
            reject_unexpected_connected_event(
                peer_mac, duplex_err == ESP_OK ? &duplex : NULL);
            return ESP_OK;
        }
        return ESP_ERR_NOT_FOUND;
    }
    if (!same_peer(peer_mac, operation.peer_mac)) {
        record_wrong_peer_event();
        return ESP_OK;
    }

    if (duplex_err != ESP_OK || operation.generation == 0U ||
        operation.generation != duplex.session_generation) {
        if (control_lock() == ESP_OK) {
            s_control.snapshot.stale_events++;
            (void)control_unlock(ESP_OK);
        }
        return ESP_OK;
    }

    const bool terminal_late =
        operation.state == BT_HFP_AUDIO_OPERATION_TIMED_OUT ||
        operation.state == BT_HFP_AUDIO_OPERATION_FAULTED ||
        operation.state == BT_HFP_AUDIO_OPERATION_REJECTED;
    if (terminal_late) {
        if (control_lock() == ESP_OK) {
            s_control.snapshot.stale_events++;
            (void)control_unlock(ESP_OK);
        }
        return ESP_OK;
    }

    if (operation.pending &&
        operation.type == BT_HFP_AUDIO_OPERATION_START) {
        esp_err_t completion = ESP_ERR_INVALID_STATE;
        bool terminal = false;
        bt_hfp_audio_operation_state_t terminal_state =
            BT_HFP_AUDIO_OPERATION_REJECTED;

        switch (state) {
        case BT_HFP_AG_AUDIO_CONNECTING:
            return ESP_OK;
        case BT_HFP_AG_AUDIO_CONNECTED_CVSD:
            err = set_audio_state_if_needed(
                operation.generation, operation.peer_mac,
                BT_HFP_AUDIO_CONNECTED_CVSD);
            if (err == ESP_OK && bt_duplex_get_snapshot(&duplex) == ESP_OK) {
                err = bt_hfp_audio_apply_duplex_state(
                    &duplex, peer_mac, sync_conn_handle,
                    preferred_frame_size);
            }
            if (err != ESP_OK) {
                bt_hfp_audio_profile_stopping();
                (void)set_audio_state_if_needed(
                    operation.generation, operation.peer_mac,
                    BT_HFP_AUDIO_FAULTED);
                set_health(operation.generation, operation.peer_mac,
                           BT_AUDIO_HEALTH_FAULTED, err,
                           "HFP callback route activation failed");
            }
            completion = err;
            terminal = true;
            terminal_state = err == ESP_OK
                ? BT_HFP_AUDIO_OPERATION_CONFIRMED
                : BT_HFP_AUDIO_OPERATION_FAULTED;
            break;
        case BT_HFP_AG_AUDIO_CONNECTED_MSBC:
            bt_hfp_audio_profile_stopping();
            (void)set_audio_state_if_needed(
                operation.generation, operation.peer_mac,
                BT_HFP_AUDIO_CONNECTED_MSBC);
            (void)set_audio_state_if_needed(
                operation.generation, operation.peer_mac,
                BT_HFP_AUDIO_FAULTED);
            set_health(operation.generation, operation.peer_mac,
                       BT_AUDIO_HEALTH_FAULTED, ESP_ERR_NOT_SUPPORTED,
                       "mSBC is unsupported during the CVSD phase");
            completion = ESP_ERR_NOT_SUPPORTED;
            terminal = true;
            terminal_state = BT_HFP_AUDIO_OPERATION_FAULTED;
            break;
        case BT_HFP_AG_AUDIO_DISCONNECTED:
            bt_hfp_audio_profile_stopping();
            (void)set_audio_state_if_needed(
                operation.generation, operation.peer_mac,
                BT_HFP_AUDIO_DISCONNECTED);
            completion = ESP_FAIL;
            terminal = true;
            terminal_state = BT_HFP_AUDIO_OPERATION_REJECTED;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
        }

        if (terminal && control_lock() == ESP_OK) {
            if (operation.serial == s_control.snapshot.serial &&
                s_control.snapshot.pending) {
                s_control.snapshot.pending = false;
                s_control.snapshot.state = terminal_state;
                s_control.snapshot.completion_result = completion;
                s_control.snapshot.last_error = completion;
            }
            (void)control_unlock(ESP_OK);
            (void)platform_binary_sem_give(s_control.event_done);
        }
        return ESP_OK;
    }

    if (operation.pending &&
        operation.type == BT_HFP_AUDIO_OPERATION_STOP) {
        if (state != BT_HFP_AG_AUDIO_DISCONNECTED) return ESP_OK;
        bt_hfp_audio_profile_stopping();
        err = set_audio_state_if_needed(
            operation.generation, operation.peer_mac,
            BT_HFP_AUDIO_DISCONNECTED);
        if (control_lock() == ESP_OK) {
            if (operation.serial == s_control.snapshot.serial &&
                s_control.snapshot.pending) {
                s_control.snapshot.pending = false;
                s_control.snapshot.state = err == ESP_OK
                    ? BT_HFP_AUDIO_OPERATION_CONFIRMED
                    : BT_HFP_AUDIO_OPERATION_FAULTED;
                s_control.snapshot.completion_result = err;
                s_control.snapshot.last_error = err;
            }
            (void)control_unlock(ESP_OK);
        }
        (void)platform_binary_sem_give(s_control.event_done);
        return ESP_OK;
    }

    if (operation.state == BT_HFP_AUDIO_OPERATION_CONFIRMED &&
        operation.type == BT_HFP_AUDIO_OPERATION_START &&
        state == BT_HFP_AG_AUDIO_DISCONNECTED) {
        bt_hfp_audio_profile_stopping();
        (void)set_audio_state_if_needed(
            operation.generation, operation.peer_mac,
            BT_HFP_AUDIO_DISCONNECTED);
        set_health(operation.generation, operation.peer_mac,
                   BT_AUDIO_HEALTH_DEGRADED, ESP_FAIL,
                   "Remote HFP audio disconnected outside explicit stop");
        dispatch_remote_cleanup(operation.generation,
                                operation.peer_mac, false, true);
        return ESP_OK;
    }

    if (state == BT_HFP_AG_AUDIO_CONNECTED_CVSD ||
        state == BT_HFP_AG_AUDIO_CONNECTED_MSBC) {
        reject_unexpected_connected_event(peer_mac, &duplex);
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}
