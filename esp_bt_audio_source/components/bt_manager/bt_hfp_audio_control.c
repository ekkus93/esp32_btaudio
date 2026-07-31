#include "bt_hfp_audio_control_internal.h"

#include <string.h>

bt_hfp_audio_control_context_t s_control;

#ifdef UNIT_TEST
bt_hfp_audio_control_platform_ops_t s_test_ops;
bool s_test_ops_set;
#endif

esp_err_t control_lock(void)
{
    if (!s_control.initialized || s_control.lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return platform_mutex_lock(s_control.lock, PLATFORM_WAIT_FOREVER);
}

esp_err_t control_unlock(esp_err_t prior)
{
    const esp_err_t err = platform_mutex_unlock(s_control.lock);
    return prior != ESP_OK ? prior : err;
}

void drain_sem(platform_binary_sem_t sem)
{
    if (sem != NULL) (void)platform_binary_sem_take(sem, 0U);
}

esp_err_t context_ensure(void)
{
    if (s_control.initialized) return ESP_OK;

    platform_mutex_t lock = platform_mutex_create();
    if (lock == NULL) return ESP_ERR_NO_MEM;
    platform_binary_sem_t request_done = platform_binary_sem_create();
    if (request_done == NULL) {
        platform_mutex_delete(lock);
        return ESP_ERR_NO_MEM;
    }
    platform_binary_sem_t event_done = platform_binary_sem_create();
    if (event_done == NULL) {
        platform_binary_sem_delete(request_done);
        platform_mutex_delete(lock);
        return ESP_ERR_NO_MEM;
    }

    memset(&s_control, 0, sizeof(s_control));
    s_control.lock = lock;
    s_control.request_done = request_done;
    s_control.event_done = event_done;
    s_control.snapshot.initialized = true;
    s_control.snapshot.type = BT_HFP_AUDIO_OPERATION_NONE;
    s_control.snapshot.state = BT_HFP_AUDIO_OPERATION_IDLE;
    s_control.snapshot.immediate_result = ESP_OK;
    s_control.snapshot.completion_result = ESP_OK;
    s_control.snapshot.cleanup_result = ESP_OK;
    s_control.snapshot.last_error = ESP_OK;
    s_control.initialized = true;
    return ESP_OK;
}

esp_err_t bt_hfp_audio_control_init(void)
{
    return context_ensure();
}

esp_err_t reserve_operation(bt_hfp_audio_operation_type_t type,
                            const char *peer_mac,
                            uint32_t *serial_out)
{
    esp_err_t err = context_ensure();
    if (err != ESP_OK) return err;
    err = control_lock();
    if (err != ESP_OK) return err;

    if (s_control.snapshot.api_active || s_control.snapshot.pending ||
        s_control.snapshot.state == BT_HFP_AUDIO_OPERATION_TIMED_OUT ||
        s_control.snapshot.state == BT_HFP_AUDIO_OPERATION_FAULTED) {
        return control_unlock(ESP_ERR_INVALID_STATE);
    }

    drain_sem(s_control.request_done);
    drain_sem(s_control.event_done);
    s_control.snapshot.serial++;
    if (s_control.snapshot.serial == 0U) s_control.snapshot.serial = 1U;
    s_control.snapshot.api_active = true;
    s_control.snapshot.pending = false;
    s_control.snapshot.lower_request_accepted = false;
    s_control.snapshot.type = type;
    s_control.snapshot.state = BT_HFP_AUDIO_OPERATION_PREPARING;
    s_control.snapshot.generation = 0U;
    copy_peer(s_control.snapshot.peer_mac, peer_mac);
    s_control.snapshot.immediate_result = ESP_ERR_INVALID_STATE;
    s_control.snapshot.completion_result = ESP_ERR_INVALID_STATE;
    s_control.snapshot.cleanup_result = ESP_OK;
    s_control.snapshot.last_error = ESP_OK;
    if (type == BT_HFP_AUDIO_OPERATION_START) {
        s_control.snapshot.start_calls++;
    } else {
        s_control.snapshot.stop_calls++;
    }
    *serial_out = s_control.snapshot.serial;
    return control_unlock(ESP_OK);
}

void update_operation_generation(uint32_t serial, uint32_t generation)
{
    if (control_lock() != ESP_OK) return;
    if (serial == s_control.snapshot.serial) {
        s_control.snapshot.generation = generation;
    }
    (void)control_unlock(ESP_OK);
}

void finish_operation(uint32_t serial, esp_err_t result,
                      bt_hfp_audio_operation_state_t failure_state)
{
    if (control_lock() != ESP_OK) return;
    if (serial == s_control.snapshot.serial) {
        s_control.snapshot.api_active = false;
        s_control.snapshot.pending = false;
        s_control.snapshot.last_error = result;
        if (result == ESP_OK) {
            s_control.snapshot.state = BT_HFP_AUDIO_OPERATION_CONFIRMED;
            if (s_control.snapshot.type == BT_HFP_AUDIO_OPERATION_START) {
                s_control.snapshot.successful_starts++;
            } else {
                s_control.snapshot.successful_stops++;
            }
        } else {
            if (s_control.snapshot.state != BT_HFP_AUDIO_OPERATION_TIMED_OUT &&
                s_control.snapshot.state != BT_HFP_AUDIO_OPERATION_FAULTED) {
                s_control.snapshot.state = failure_state;
            }
            if (s_control.snapshot.type == BT_HFP_AUDIO_OPERATION_START) {
                s_control.snapshot.start_failures++;
            } else {
                s_control.snapshot.stop_failures++;
            }
        }
    }
    (void)control_unlock(ESP_OK);
}

esp_err_t bt_hfp_audio_start(void)
{
    esp_err_t err = context_ensure();
    if (err != ESP_OK) return err;

    bt_hfp_audio_snapshot_t callback;
    err = bt_hfp_audio_get_snapshot(&callback);
    if (err != ESP_OK || !callback.callback_registered) {
        return ESP_ERR_INVALID_STATE;
    }

    bt_duplex_snapshot_t duplex;
    err = bt_duplex_get_snapshot(&duplex);
    if (err != ESP_OK) return err;
    if (!duplex.peer_valid ||
        duplex.requested_mode == BT_DUPLEX_MODE_DISABLED ||
        duplex.effective_mode == BT_DUPLEX_MODE_DISABLED ||
        duplex.hfp_profile_state != BT_HFP_PROFILE_SLC_CONNECTED ||
        duplex.hfp_audio_state != BT_HFP_AUDIO_DISCONNECTED ||
        duplex.i2s_state != BT_HFP_I2S_STOPPED ||
        duplex.health >= BT_AUDIO_HEALTH_FAULTED) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t serial = 0U;
    err = reserve_operation(BT_HFP_AUDIO_OPERATION_START,
                            duplex.peer_mac, &serial);
    if (err != ESP_OK) return err;

    uint32_t generation = 0U;
    err = bt_duplex_audio_session_begin(duplex.peer_mac, &generation);
    if (err != ESP_OK) {
        finish_operation(serial, err, BT_HFP_AUDIO_OPERATION_REJECTED);
        return err;
    }
    update_operation_generation(serial, generation);

    err = start_i2s_for_session(generation, duplex.peer_mac);
    if (err != ESP_OK) {
        finish_operation(serial, err, BT_HFP_AUDIO_OPERATION_FAULTED);
        return err;
    }

    err = set_audio_state_if_needed(
        generation, duplex.peer_mac, BT_HFP_AUDIO_CONNECTING);
    if (err != ESP_OK) {
        const esp_err_t rollback = rollback_started_i2s(
            serial, generation, duplex.peer_mac, err, false);
        finish_operation(serial, rollback, BT_HFP_AUDIO_OPERATION_FAULTED);
        return rollback;
    }

    err = queue_lower_request(serial, BT_HFP_AUDIO_OPERATION_START);
    if (err != ESP_OK) {
        const bool disconnect_lower =
            err == ESP_ERR_TIMEOUT || lower_request_may_be_live(serial);
        const esp_err_t rollback = rollback_started_i2s(
            serial, generation, duplex.peer_mac, err, disconnect_lower);
        finish_operation(serial, rollback, BT_HFP_AUDIO_OPERATION_REJECTED);
        return rollback;
    }

    err = platform_binary_sem_take(
        s_control.event_done, BT_HFP_AUDIO_EVENT_TIMEOUT_MS);
    if (err != ESP_OK) {
        if (control_lock() == ESP_OK) {
            if (serial == s_control.snapshot.serial) {
                s_control.snapshot.state = BT_HFP_AUDIO_OPERATION_TIMED_OUT;
                s_control.snapshot.pending = false;
                s_control.snapshot.event_timeouts++;
                s_control.snapshot.last_error = err;
            }
            (void)control_unlock(ESP_OK);
        }
        bt_hfp_audio_profile_stopping();
        (void)set_audio_state_if_needed(
            generation, duplex.peer_mac, BT_HFP_AUDIO_FAULTED);
        set_health(generation, duplex.peer_mac, BT_AUDIO_HEALTH_FAULTED,
                   err, "HFP audio connect event timed out");
        const esp_err_t rollback = rollback_started_i2s(
            serial, generation, duplex.peer_mac, err, true);
        finish_operation(serial, rollback, BT_HFP_AUDIO_OPERATION_TIMED_OUT);
        return rollback;
    }

    esp_err_t completion = ESP_FAIL;
    bt_hfp_audio_operation_state_t state = BT_HFP_AUDIO_OPERATION_REJECTED;
    bool accepted = false;
    if (control_lock() == ESP_OK) {
        if (serial == s_control.snapshot.serial) {
            completion = s_control.snapshot.completion_result;
            state = s_control.snapshot.state;
            accepted = s_control.snapshot.lower_request_accepted;
        }
        (void)control_unlock(ESP_OK);
    }
    if (completion != ESP_OK || state != BT_HFP_AUDIO_OPERATION_CONFIRMED) {
        const esp_err_t rollback = rollback_started_i2s(
            serial, generation, duplex.peer_mac,
            completion != ESP_OK ? completion : ESP_FAIL, accepted);
        finish_operation(serial, rollback, state);
        return rollback;
    }

    finish_operation(serial, ESP_OK, BT_HFP_AUDIO_OPERATION_CONFIRMED);
    return ESP_OK;
}

esp_err_t bt_hfp_audio_stop(void)
{
    esp_err_t err = context_ensure();
    if (err != ESP_OK) return err;

    bt_duplex_snapshot_t duplex;
    err = bt_duplex_get_snapshot(&duplex);
    if (err != ESP_OK) return err;
    if (!duplex.peer_valid) return ESP_ERR_INVALID_STATE;
    if (duplex.hfp_audio_state == BT_HFP_AUDIO_DISCONNECTED &&
        duplex.i2s_state == BT_HFP_I2S_STOPPED) {
        return ESP_OK;
    }
    if (duplex.hfp_profile_state != BT_HFP_PROFILE_SLC_CONNECTED ||
        duplex.i2s_state == BT_HFP_I2S_QUARANTINED) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t serial = 0U;
    err = reserve_operation(BT_HFP_AUDIO_OPERATION_STOP,
                            duplex.peer_mac, &serial);
    if (err != ESP_OK) return err;
    update_operation_generation(serial, duplex.session_generation);
    bt_hfp_audio_profile_stopping();

    const bool request_disconnect =
        duplex.hfp_audio_state != BT_HFP_AUDIO_DISCONNECTED;
    if (request_disconnect) {
        err = set_audio_state_if_needed(
            duplex.session_generation, duplex.peer_mac,
            BT_HFP_AUDIO_DISCONNECTING);
        if (err != ESP_OK) {
            finish_operation(serial, err, BT_HFP_AUDIO_OPERATION_FAULTED);
            return err;
        }

        err = queue_lower_request(serial, BT_HFP_AUDIO_OPERATION_STOP);
        if (err != ESP_OK) {
            (void)set_audio_state_if_needed(
                duplex.session_generation, duplex.peer_mac,
                BT_HFP_AUDIO_FAULTED);
            set_health(duplex.session_generation, duplex.peer_mac,
                       BT_AUDIO_HEALTH_FAULTED, err,
                       "HFP audio disconnect request failed");
            esp_err_t stop_result = ESP_OK;
            stop_i2s_for_session(duplex.session_generation,
                                 duplex.peer_mac, &stop_result);
            const esp_err_t result =
                stop_result != ESP_OK ? stop_result : err;
            finish_operation(serial, result, BT_HFP_AUDIO_OPERATION_FAULTED);
            return result;
        }

        err = platform_binary_sem_take(
            s_control.event_done, BT_HFP_AUDIO_EVENT_TIMEOUT_MS);
        if (err != ESP_OK) {
            if (control_lock() == ESP_OK) {
                if (serial == s_control.snapshot.serial) {
                    s_control.snapshot.state = BT_HFP_AUDIO_OPERATION_TIMED_OUT;
                    s_control.snapshot.pending = false;
                    s_control.snapshot.event_timeouts++;
                    s_control.snapshot.last_error = err;
                }
                (void)control_unlock(ESP_OK);
            }
            (void)set_audio_state_if_needed(
                duplex.session_generation, duplex.peer_mac,
                BT_HFP_AUDIO_FAULTED);
            set_health(duplex.session_generation, duplex.peer_mac,
                       BT_AUDIO_HEALTH_FAULTED, err,
                       "HFP audio disconnect event timed out");
            esp_err_t stop_result = ESP_OK;
            stop_i2s_for_session(duplex.session_generation,
                                 duplex.peer_mac, &stop_result);
            const esp_err_t result =
                stop_result != ESP_OK ? stop_result : err;
            finish_operation(serial, result, BT_HFP_AUDIO_OPERATION_TIMED_OUT);
            return result;
        }

        esp_err_t completion = ESP_FAIL;
        if (control_lock() == ESP_OK) {
            completion = s_control.snapshot.completion_result;
            (void)control_unlock(ESP_OK);
        }
        if (completion != ESP_OK) {
            (void)set_audio_state_if_needed(
                duplex.session_generation, duplex.peer_mac,
                BT_HFP_AUDIO_FAULTED);
            set_health(duplex.session_generation, duplex.peer_mac,
                       BT_AUDIO_HEALTH_FAULTED, completion,
                       "HFP audio disconnect was not confirmed");
        }
    }

    esp_err_t stop_result = ESP_OK;
    stop_i2s_for_session(duplex.session_generation,
                         duplex.peer_mac, &stop_result);
    if (stop_result != ESP_OK) {
        finish_operation(serial, stop_result, BT_HFP_AUDIO_OPERATION_FAULTED);
        return stop_result;
    }

    if (request_disconnect) {
        esp_err_t completion = ESP_FAIL;
        if (control_lock() == ESP_OK) {
            completion = s_control.snapshot.completion_result;
            (void)control_unlock(ESP_OK);
        }
        if (completion != ESP_OK) {
            finish_operation(serial, completion,
                             BT_HFP_AUDIO_OPERATION_FAULTED);
            return completion;
        }
    }

    finish_operation(serial, ESP_OK, BT_HFP_AUDIO_OPERATION_CONFIRMED);
    return ESP_OK;
}

esp_err_t bt_hfp_audio_control_get_snapshot(
    bt_hfp_audio_control_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    const esp_err_t err = control_lock();
    if (err != ESP_OK) return err;
    *out = s_control.snapshot;
    return control_unlock(ESP_OK);
}

void bt_hfp_audio_control_profile_stopping(void)
{
    /* The fast callback gate must close even if no start/stop API has ever
     * initialized the control context. */
    bt_hfp_audio_profile_stopping();
    if (!s_control.initialized) return;
    if (control_lock() != ESP_OK) return;
    if (s_control.snapshot.api_active || s_control.snapshot.pending) {
        s_control.snapshot.pending = false;
        s_control.snapshot.state = BT_HFP_AUDIO_OPERATION_FAULTED;
        s_control.snapshot.completion_result = ESP_ERR_INVALID_STATE;
        s_control.snapshot.last_error = ESP_ERR_INVALID_STATE;
    }
    (void)control_unlock(ESP_OK);
    (void)platform_binary_sem_give(s_control.request_done);
    (void)platform_binary_sem_give(s_control.event_done);
}

esp_err_t bt_hfp_audio_control_cleanup_after_stack_shutdown(void)
{
    if (!s_control.initialized) return ESP_OK;
    esp_err_t err = control_lock();
    if (err != ESP_OK) return err;
    if (s_control.snapshot.api_active || s_control.snapshot.pending) {
        return control_unlock(ESP_ERR_INVALID_STATE);
    }
    platform_mutex_t lock = s_control.lock;
    platform_binary_sem_t request_done = s_control.request_done;
    platform_binary_sem_t event_done = s_control.event_done;
    memset(&s_control, 0, sizeof(s_control));
    (void)platform_mutex_unlock(lock);
    platform_binary_sem_delete(request_done);
    platform_binary_sem_delete(event_done);
    platform_mutex_delete(lock);
    return ESP_OK;
}

#ifdef UNIT_TEST
esp_err_t bt_hfp_audio_control_test_set_platform_ops(
    const bt_hfp_audio_control_platform_ops_t *ops)
{
    if (ops == NULL || ops->audio_connect == NULL ||
        ops->audio_disconnect == NULL || s_control.initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    s_test_ops = *ops;
    s_test_ops_set = true;
    return ESP_OK;
}

void bt_hfp_audio_control_test_reset(void)
{
    if (s_control.initialized) {
        s_control.snapshot.api_active = false;
        s_control.snapshot.pending = false;
        (void)bt_hfp_audio_control_cleanup_after_stack_shutdown();
    }
    memset(&s_control, 0, sizeof(s_control));
    memset(&s_test_ops, 0, sizeof(s_test_ops));
    s_test_ops_set = false;
}
#endif
