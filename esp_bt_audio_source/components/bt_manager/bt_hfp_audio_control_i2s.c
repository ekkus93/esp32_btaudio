#include "bt_hfp_audio_control_internal.h"

#include <string.h>

#include "hfp_i2s_output.h"

static bt_hfp_i2s_state_t map_i2s_state(hfp_i2s_output_state_t state)
{
    switch (state) {
    case HFP_I2S_OUTPUT_UNINITIALIZED:
    case HFP_I2S_OUTPUT_STOPPED:
        return BT_HFP_I2S_STOPPED;
    case HFP_I2S_OUTPUT_STARTING:
        return BT_HFP_I2S_STARTING;
    case HFP_I2S_OUTPUT_RUNNING:
        return BT_HFP_I2S_RUNNING;
    case HFP_I2S_OUTPUT_STOPPING:
        return BT_HFP_I2S_STOPPING;
    case HFP_I2S_OUTPUT_FAULTED:
        return BT_HFP_I2S_FAULTED;
    case HFP_I2S_OUTPUT_QUARANTINED:
        return BT_HFP_I2S_QUARANTINED;
    default:
        return BT_HFP_I2S_FAULTED;
    }
}

static esp_err_t set_i2s_state_if_needed(uint32_t generation,
                                         const char *peer_mac,
                                         bt_hfp_i2s_state_t state)
{
    bt_duplex_snapshot_t duplex;
    esp_err_t err = bt_duplex_get_snapshot(&duplex);
    if (err != ESP_OK) return err;
    if (duplex.i2s_state == state) return ESP_OK;
    return bt_duplex_set_i2s_state(generation, peer_mac, state);
}

esp_err_t set_audio_state_if_needed(uint32_t generation,
                                    const char *peer_mac,
                                    bt_hfp_audio_state_t state)
{
    bt_duplex_snapshot_t duplex;
    esp_err_t err = bt_duplex_get_snapshot(&duplex);
    if (err != ESP_OK) return err;
    if (duplex.hfp_audio_state == state) return ESP_OK;
    return bt_duplex_set_hfp_audio_state(generation, peer_mac, state);
}

void set_health(uint32_t generation, const char *peer_mac,
               bt_audio_health_t health, esp_err_t error,
               const char *text)
{
    (void)bt_duplex_set_health(generation, peer_mac, health, error, text);
}

static esp_err_t local_i2s_snapshot(hfp_i2s_output_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    esp_err_t err = hfp_i2s_output_get_snapshot(out);
    if (err == ESP_ERR_INVALID_STATE) {
        out->state = HFP_I2S_OUTPUT_UNINITIALIZED;
        out->last_error = ESP_OK;
        return ESP_OK;
    }
    return err;
}

static esp_err_t sync_i2s_state(uint32_t generation, const char *peer_mac,
                                hfp_i2s_output_snapshot_t *snapshot_out)
{
    hfp_i2s_output_snapshot_t local = {0};
    esp_err_t err = local_i2s_snapshot(&local);
    if (err != ESP_OK) return err;
    err = set_i2s_state_if_needed(generation, peer_mac,
                                  map_i2s_state(local.state));
    if (snapshot_out != NULL) *snapshot_out = local;
    return err;
}

static esp_err_t ensure_i2s_initialized(void)
{
    hfp_i2s_output_snapshot_t local = {0};
    esp_err_t err = local_i2s_snapshot(&local);
    if (err != ESP_OK) return err;
    if (local.initialized) {
        return local.state == HFP_I2S_OUTPUT_STOPPED
            ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    hfp_i2s_output_config_t config;
    hfp_i2s_pin_owners_t owners;
    err = hfp_i2s_output_default_config(&config);
    if (err != ESP_OK) return err;
    err = hfp_i2s_output_get_runtime_pin_owners(&owners);
    if (err != ESP_OK) return err;
    return hfp_i2s_output_init(&config, &owners);
}

esp_err_t start_i2s_for_session(uint32_t generation,
                                const char *peer_mac)
{
    esp_err_t err = set_i2s_state_if_needed(
        generation, peer_mac, BT_HFP_I2S_STARTING);
    if (err != ESP_OK) return err;

    err = ensure_i2s_initialized();
    if (err == ESP_OK) {
        err = hfp_i2s_output_start(generation, peer_mac);
    }
    if (err != ESP_OK) {
        hfp_i2s_output_snapshot_t local = {0};
        const esp_err_t sync_err =
            sync_i2s_state(generation, peer_mac, &local);
        if (sync_err != ESP_OK) {
            (void)set_i2s_state_if_needed(
                generation, peer_mac, BT_HFP_I2S_FAULTED);
        }
        /* The primary I2S start failure (err) is always the authoritative
         * cause. A secondary failure to sync duplex state afterward
         * (sync_err) must never overwrite it -- it is made visible instead
         * via i2s_state_sync_failures, so a compound failure never hides its
         * root cause. */
        if (control_lock() == ESP_OK) {
            s_control.snapshot.i2s_start_failures++;
            if (sync_err != ESP_OK) {
                s_control.snapshot.i2s_state_sync_failures++;
            }
            s_control.snapshot.last_error = err;
            (void)control_unlock(ESP_OK);
        }
        if (sync_err == ESP_OK &&
            local.state == HFP_I2S_OUTPUT_QUARANTINED) {
            set_health(generation, peer_mac, BT_AUDIO_HEALTH_QUARANTINED,
                       err, "HFP I2S startup rollback quarantined");
        } else if (sync_err != ESP_OK) {
            set_health(generation, peer_mac, BT_AUDIO_HEALTH_FAULTED,
                       err,
                       "HFP I2S startup failed (state sync also failed)");
        } else {
            set_health(generation, peer_mac, BT_AUDIO_HEALTH_FAULTED,
                       err, "HFP I2S startup failed");
        }
        return err;
    }

    return set_i2s_state_if_needed(generation, peer_mac, BT_HFP_I2S_RUNNING);
}

static void record_i2s_stop_failure(uint32_t generation, const char *peer_mac,
                                    esp_err_t result,
                                    const hfp_i2s_output_snapshot_t *after,
                                    bool snapshot_valid,
                                    bool sync_failed)
{
    if (control_lock() == ESP_OK) {
        s_control.snapshot.i2s_stop_failures++;
        if (sync_failed) {
            s_control.snapshot.i2s_state_sync_failures++;
        }
        s_control.snapshot.last_error = result;
        (void)control_unlock(ESP_OK);
    }
    if (snapshot_valid && after != NULL &&
        after->state == HFP_I2S_OUTPUT_QUARANTINED) {
        set_health(generation, peer_mac, BT_AUDIO_HEALTH_QUARANTINED,
                   result, "HFP I2S stop quarantined");
    } else if (sync_failed) {
        set_health(generation, peer_mac, BT_AUDIO_HEALTH_FAULTED,
                   result, "HFP I2S stop failed (state sync also failed)");
    } else {
        set_health(generation, peer_mac, BT_AUDIO_HEALTH_FAULTED,
                   result, "HFP I2S stop failed");
    }
}

void stop_i2s_for_session(uint32_t generation, const char *peer_mac,
                          esp_err_t *result_out)
{
    esp_err_t result = ESP_OK;
    hfp_i2s_output_snapshot_t local = {0};
    esp_err_t err = local_i2s_snapshot(&local);
    if (err != ESP_OK) {
        result = err;
        goto done;
    }

    if (!local.initialized || local.state == HFP_I2S_OUTPUT_STOPPED ||
        local.state == HFP_I2S_OUTPUT_UNINITIALIZED) {
        result = set_i2s_state_if_needed(
            generation, peer_mac, BT_HFP_I2S_STOPPED);
        goto done;
    }

    if (local.state == HFP_I2S_OUTPUT_QUARANTINED) {
        result = set_i2s_state_if_needed(
            generation, peer_mac, BT_HFP_I2S_QUARANTINED);
        if (result == ESP_OK) result = ESP_ERR_INVALID_STATE;
        goto done;
    }

    if (local.state != HFP_I2S_OUTPUT_RUNNING &&
        local.state != HFP_I2S_OUTPUT_FAULTED) {
        result = ESP_ERR_INVALID_STATE;
        goto done;
    }

    /* The authoritative transition to STOPPING is legal only from RUNNING.
     * A faulted local writer is stopped directly and then synchronized to its
     * actual terminal state; we never fabricate FAULTED -> STOPPING. */
    if (local.state == HFP_I2S_OUTPUT_RUNNING) {
        err = set_i2s_state_if_needed(
            generation, peer_mac, BT_HFP_I2S_STOPPING);
        if (err != ESP_OK) {
            result = err;
            goto done;
        }
    }

    const uint32_t timeout_ms = local.config.stop_timeout_ms != 0U
        ? local.config.stop_timeout_ms
        : BT_HFP_AUDIO_FALLBACK_I2S_STOP_TIMEOUT_MS;
    err = hfp_i2s_output_stop(timeout_ms);
    hfp_i2s_output_snapshot_t after = {0};
    const esp_err_t sync_err =
        sync_i2s_state(generation, peer_mac, &after);
    /* The primary stop failure (err) is always the authoritative cause.
     * sync_err only becomes the reported result when the primary stop
     * itself succeeded but the follow-up duplex-state sync failed. */
    result = err != ESP_OK ? err : sync_err;
    if (result != ESP_OK) {
        record_i2s_stop_failure(generation, peer_mac, result, &after,
                                sync_err == ESP_OK,
                                err != ESP_OK && sync_err != ESP_OK);
    }

done:
    if (result_out != NULL) *result_out = result;
}
