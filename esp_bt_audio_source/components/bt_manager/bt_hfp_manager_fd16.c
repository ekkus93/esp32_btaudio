#include "bt_duplex_policy.h"

#include <string.h>
#include <strings.h>

#include "bt_duplex_state_internal.h"
#include "bt_hfp_event_contract.h"
#include "bt_manager_internal.h"
#include "platform_sync.h"

typedef struct {
    bool a2dp_interrupted_during_sco;
    bt_duplex_policy_reason_t interruption_reason;
    bool input_valid;
    bt_duplex_policy_input_t last_input;
    bt_duplex_policy_snapshot_t snapshot;
} bt_hfp_policy_runtime_t;

static bt_hfp_policy_runtime_t s_policy;

static bool valid_peer(const bt_duplex_snapshot_t *duplex,
                       uint32_t generation,
                       const char *peer_mac)
{
    return duplex != NULL && duplex->peer_valid && peer_mac != NULL &&
           generation != 0U && generation == duplex->session_generation &&
           strcasecmp(peer_mac, duplex->peer_mac) == 0;
}

static bool sco_connected(bt_hfp_audio_state_t state)
{
    return state == BT_HFP_AUDIO_CONNECTED_CVSD ||
           state == BT_HFP_AUDIO_CONNECTED_MSBC;
}

static bool same_input(const bt_duplex_policy_input_t *lhs,
                       const bt_duplex_policy_input_t *rhs)
{
    return lhs->requested == rhs->requested &&
           lhs->current_effective == rhs->current_effective &&
           lhs->a2dp_connected == rhs->a2dp_connected &&
           lhs->a2dp_streaming == rhs->a2dp_streaming &&
           lhs->hfp_slc_connected == rhs->hfp_slc_connected &&
           lhs->sco_connected == rhs->sco_connected &&
           lhs->a2dp_interrupted_during_sco ==
               rhs->a2dp_interrupted_during_sco &&
           lhs->interruption_reason == rhs->interruption_reason;
}

static bt_hfp_mode_event_reason_t event_reason(
    bt_duplex_policy_reason_t reason)
{
    switch (reason) {
    case BT_DUPLEX_POLICY_REASON_REMOTE_SUSPENDED_A2DP_DURING_SCO:
        return BT_HFP_MODE_EVENT_REASON_REMOTE_SUSPENDED_A2DP_DURING_SCO;
    case BT_DUPLEX_POLICY_REASON_A2DP_STOPPED_DURING_SCO:
        return BT_HFP_MODE_EVENT_REASON_A2DP_STOPPED_DURING_SCO;
    case BT_DUPLEX_POLICY_REASON_A2DP_RESUMED:
        return BT_HFP_MODE_EVENT_REASON_A2DP_RESUMED;
    case BT_DUPLEX_POLICY_REASON_SCO_STOPPED:
        return BT_HFP_MODE_EVENT_REASON_SCO_STOPPED;
    default:
        return BT_HFP_MODE_EVENT_REASON_POLICY;
    }
}

void bt_manager_hfp_policy_runtime_reset(void)
{
    memset(&s_policy, 0, sizeof(s_policy));
    s_policy.interruption_reason = BT_DUPLEX_POLICY_REASON_REQUESTED_MODE;
    s_policy.snapshot.reason = BT_DUPLEX_POLICY_REASON_REQUESTED_MODE;
    s_policy.snapshot.downlink_owner = BT_DUPLEX_DOWNLINK_OWNER_A2DP;
}

void bt_manager_hfp_policy_copy_locked(bt_duplex_policy_snapshot_t *out)
{
    if (out != NULL) *out = s_policy.snapshot;
}

static void begin_generation_locked(uint32_t generation)
{
    if (!s_policy.snapshot.initialized ||
        s_policy.snapshot.generation == generation) {
        return;
    }

    const uint64_t evaluations = s_policy.snapshot.evaluations_lifetime;
    const uint64_t mode_changes =
        s_policy.snapshot.effective_mode_changes_lifetime;
    const uint64_t incompatibilities =
        s_policy.snapshot.incompatibilities_lifetime;
    const uint64_t interruptions =
        s_policy.snapshot.a2dp_interruptions_lifetime;

    memset(&s_policy, 0, sizeof(s_policy));
    s_policy.interruption_reason = BT_DUPLEX_POLICY_REASON_REQUESTED_MODE;
    s_policy.snapshot.reason = BT_DUPLEX_POLICY_REASON_REQUESTED_MODE;
    s_policy.snapshot.downlink_owner = BT_DUPLEX_DOWNLINK_OWNER_A2DP;
    s_policy.snapshot.evaluations_lifetime = evaluations;
    s_policy.snapshot.effective_mode_changes_lifetime = mode_changes;
    s_policy.snapshot.incompatibilities_lifetime = incompatibilities;
    s_policy.snapshot.a2dp_interruptions_lifetime = interruptions;
}

static esp_err_t refresh_from_duplex_locked(bt_duplex_snapshot_t *duplex)
{
    if (duplex == NULL || !duplex->peer_valid ||
        duplex->session_generation == 0U) {
        return ESP_ERR_NOT_FOUND;
    }

    begin_generation_locked(duplex->session_generation);

    bt_duplex_mode_t prior_effective = duplex->effective_mode;
    if (s_policy.snapshot.initialized &&
        s_policy.snapshot.generation == duplex->session_generation) {
        prior_effective = s_policy.snapshot.effective;
    }

    const bt_duplex_policy_input_t input = {
        .requested = duplex->requested_mode,
        .current_effective = prior_effective,
        .a2dp_connected = duplex->a2dp_profile_state ==
                          BT_A2DP_PROFILE_CONNECTED,
        .a2dp_streaming = duplex->a2dp_audio_state == BT_A2DP_AUDIO_STARTED,
        .hfp_slc_connected = duplex->hfp_profile_state ==
                             BT_HFP_PROFILE_SLC_CONNECTED,
        .sco_connected = sco_connected(duplex->hfp_audio_state),
        .a2dp_interrupted_during_sco =
            s_policy.a2dp_interrupted_during_sco,
        .interruption_reason = s_policy.interruption_reason,
    };

    if (s_policy.input_valid && same_input(&s_policy.last_input, &input)) {
        return ESP_OK;
    }

    bt_duplex_policy_result_t result;
    esp_err_t err = bt_duplex_policy_evaluate(&input, &result);
    if (err != ESP_OK) return err;

    bt_duplex_policy_snapshot_t next = s_policy.snapshot;
    const bool was_incompatible = next.initialized &&
        next.state == BT_DUPLEX_POLICY_INCOMPATIBLE;
    next.initialized = true;
    next.generation = duplex->session_generation;
    next.requested = duplex->requested_mode;
    next.effective = result.effective;
    next.state = result.state;
    next.reason = result.reason;
    next.downlink_owner = result.downlink_owner;
    next.request_hfp_downlink = result.request_hfp_downlink;
    if (next.evaluations_lifetime != UINT64_MAX) {
        next.evaluations_lifetime++;
    }
    if (!was_incompatible && result.state == BT_DUPLEX_POLICY_INCOMPATIBLE &&
        next.incompatibilities_lifetime != UINT64_MAX) {
        next.incompatibilities_lifetime++;
    }

    const bt_duplex_mode_t old_effective = duplex->effective_mode;
    if (old_effective != result.effective) {
        err = bt_duplex_set_effective_mode(
            duplex->session_generation, duplex->peer_mac, result.effective);
        if (err != ESP_OK) return err;
        duplex->effective_mode = result.effective;
        if (next.effective_mode_changes_lifetime != UINT64_MAX) {
            next.effective_mode_changes_lifetime++;
        }
        esp_err_t emit = bt_hfp_event_emit_mode(
            old_effective, result.effective, event_reason(result.reason),
            duplex->session_generation);
        if (emit != ESP_OK) {
            bt_duplex_record_event_delivery_failure(
                duplex->session_generation, duplex->peer_mac, emit);
        }
    }

    s_policy.last_input = input;
    s_policy.input_valid = true;
    s_policy.snapshot = next;
    return ESP_OK;
}

esp_err_t bt_manager_hfp_policy_refresh_locked(void)
{
    bt_duplex_snapshot_t duplex;
    esp_err_t err = bt_duplex_get_snapshot(&duplex);
    if (err != ESP_OK) return err;
    return refresh_from_duplex_locked(&duplex);
}

esp_err_t bt_manager_hfp_policy_refresh(void)
{
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    err = bt_manager_hfp_policy_refresh_locked();
    bt_ctx_unlock();
    return err;
}

esp_err_t bt_manager_hfp_get_policy_snapshot(
    bt_duplex_policy_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    *out = s_policy.snapshot;
    bt_ctx_unlock();
    return ESP_OK;
}

static esp_err_t get_bound_duplex_locked(const char *peer_mac,
                                         bt_duplex_snapshot_t *duplex)
{
    esp_err_t err = bt_duplex_get_snapshot(duplex);
    if (err != ESP_OK) return err;
    if (!duplex->peer_valid || peer_mac == NULL ||
        strcasecmp(peer_mac, duplex->peer_mac) != 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t bt_manager_hfp_handle_a2dp_profile_event(
    const char *peer_mac,
    bt_a2dp_profile_state_t state)
{
    if (peer_mac == NULL || (int)state < 0 ||
        state >= BT_A2DP_PROFILE_STATE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    bt_duplex_mode_t configured_mode;
    esp_err_t err = bt_manager_hfp_get_configured_mode(&configured_mode);
    if (err != ESP_OK) return err;

    err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    bt_duplex_snapshot_t duplex;
    err = bt_duplex_get_snapshot(&duplex);
    if (err == ESP_OK && !duplex.peer_valid) {
        if (state == BT_A2DP_PROFILE_DISCONNECTED ||
            state == BT_A2DP_PROFILE_DISCONNECTING) {
            bt_ctx_unlock();
            return ESP_ERR_NOT_FOUND;
        }
        uint32_t generation = 0U;
        err = bt_duplex_session_begin(peer_mac, configured_mode, &generation);
        if (err == ESP_OK) err = bt_duplex_get_snapshot(&duplex);
    }
    if (err == ESP_OK && strcasecmp(peer_mac, duplex.peer_mac) != 0) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK && state == BT_A2DP_PROFILE_CONNECTED &&
        duplex.a2dp_profile_state == BT_A2DP_PROFILE_DISCONNECTED) {
        err = bt_duplex_set_a2dp_profile_state(
            duplex.session_generation, peer_mac, BT_A2DP_PROFILE_CONNECTING);
    }
    if (err == ESP_OK) {
        err = bt_duplex_set_a2dp_profile_state(
            duplex.session_generation, peer_mac, state);
    }
    if (err == ESP_OK) err = bt_duplex_get_snapshot(&duplex);
    if (err == ESP_OK) err = refresh_from_duplex_locked(&duplex);
    bt_ctx_unlock();
    return err;
}

esp_err_t bt_manager_hfp_handle_a2dp_audio_event(
    const char *peer_mac,
    bt_a2dp_audio_state_t state)
{
    if (peer_mac == NULL || (int)state < 0 ||
        state >= BT_A2DP_AUDIO_STATE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    bt_duplex_snapshot_t duplex;
    memset(&duplex, 0, sizeof(duplex));
    err = get_bound_duplex_locked(peer_mac, &duplex);
    if (err != ESP_OK) {
        bt_ctx_unlock();
        return err;
    }

    const bt_a2dp_audio_state_t old_state = duplex.a2dp_audio_state;
    err = bt_duplex_set_a2dp_audio_state(
        duplex.session_generation, peer_mac, state);
    if (err == ESP_OK && state == BT_A2DP_AUDIO_STARTED) {
        s_policy.a2dp_interrupted_during_sco = false;
        s_policy.interruption_reason = BT_DUPLEX_POLICY_REASON_A2DP_RESUMED;
    } else if (err == ESP_OK && sco_connected(duplex.hfp_audio_state) &&
               old_state == BT_A2DP_AUDIO_STARTED &&
               (state == BT_A2DP_AUDIO_REMOTE_SUSPENDED ||
                state == BT_A2DP_AUDIO_STOPPED)) {
        if (!s_policy.a2dp_interrupted_during_sco &&
            s_policy.snapshot.a2dp_interruptions_lifetime != UINT64_MAX) {
            s_policy.snapshot.a2dp_interruptions_lifetime++;
        }
        s_policy.a2dp_interrupted_during_sco = true;
        s_policy.interruption_reason =
            state == BT_A2DP_AUDIO_REMOTE_SUSPENDED
                ? BT_DUPLEX_POLICY_REASON_REMOTE_SUSPENDED_A2DP_DURING_SCO
                : BT_DUPLEX_POLICY_REASON_A2DP_STOPPED_DURING_SCO;
    }
    if (err == ESP_OK) err = bt_duplex_get_snapshot(&duplex);
    if (err == ESP_OK) err = refresh_from_duplex_locked(&duplex);
    bt_ctx_unlock();
    return err;
}

esp_err_t bt_manager_hfp_policy_note_hfp_profile_transition(
    uint32_t generation,
    const char *peer_mac,
    bt_hfp_profile_state_t old_state,
    bt_hfp_profile_state_t new_state)
{
    (void)old_state;
    (void)new_state;
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    bt_duplex_snapshot_t duplex;
    err = bt_duplex_get_snapshot(&duplex);
    if (err == ESP_OK && !valid_peer(&duplex, generation, peer_mac)) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK) err = refresh_from_duplex_locked(&duplex);
    bt_ctx_unlock();
    return err;
}

esp_err_t bt_manager_hfp_policy_note_hfp_audio_transition(
    uint32_t generation,
    const char *peer_mac,
    bt_hfp_audio_state_t old_state,
    bt_hfp_audio_state_t new_state)
{
    (void)old_state;
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    bt_duplex_snapshot_t duplex;
    err = bt_duplex_get_snapshot(&duplex);
    if (err == ESP_OK && !valid_peer(&duplex, generation, peer_mac)) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK && new_state == BT_HFP_AUDIO_DISCONNECTED) {
        s_policy.a2dp_interrupted_during_sco = false;
        s_policy.interruption_reason = BT_DUPLEX_POLICY_REASON_SCO_STOPPED;
    }
    if (err == ESP_OK) err = refresh_from_duplex_locked(&duplex);
    bt_ctx_unlock();
    return err;
}
