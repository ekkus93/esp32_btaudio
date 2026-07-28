#include "bt_hfp_ag_internal.h"
#include "bt_hfp_audio.h"
#include "bt_hfp_connection.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_bt_main.h"
#include "esp_hf_ag_api.h"
#include "esp_log.h"
static const char *TAG = "BT_HFP_AG";
#endif

bt_hfp_ag_context_t g_bt_hfp_ag;

static void snapshot_defaults(bt_hfp_ag_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->lifecycle = BT_HFP_AG_LIFECYCLE_UNINITIALIZED;
    snapshot->last_error = ESP_OK;
    snapshot->last_codec_event = BT_HFP_AG_CODEC_NONE;
    snapshot->speaker_volume = -1;
    snapshot->microphone_volume = -1;
}

esp_err_t bt_hfp_ag_context_ensure(void)
{
    if (g_bt_hfp_ag.resources_ready) return ESP_OK;
    platform_mutex_t lock = platform_mutex_create();
    if (lock == NULL) return ESP_ERR_NO_MEM;
    platform_binary_sem_t completion = platform_binary_sem_create();
    if (completion == NULL) {
        platform_mutex_delete(lock);
        return ESP_ERR_NO_MEM;
    }
    memset(&g_bt_hfp_ag, 0, sizeof(g_bt_hfp_ag));
    g_bt_hfp_ag.lock = lock;
    g_bt_hfp_ag.completion = completion;
    snapshot_defaults(&g_bt_hfp_ag.snapshot);
    g_bt_hfp_ag.resources_ready = true;
    return ESP_OK;
}

esp_err_t bt_hfp_ag_lock(void)
{
    if (!g_bt_hfp_ag.resources_ready || g_bt_hfp_ag.lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return platform_mutex_lock(g_bt_hfp_ag.lock, PLATFORM_WAIT_FOREVER);
}

esp_err_t bt_hfp_ag_unlock(esp_err_t prior)
{
    esp_err_t err = platform_mutex_unlock(g_bt_hfp_ag.lock);
    return prior != ESP_OK ? prior : err;
}

void bt_hfp_ag_set_fault_locked(esp_err_t error)
{
    g_bt_hfp_ag.snapshot.lifecycle = BT_HFP_AG_LIFECYCLE_FAULTED;
    g_bt_hfp_ag.snapshot.profile_ready = false;
    g_bt_hfp_ag.snapshot.last_error = error;
}

void bt_hfp_ag_remember_peer_locked(const char *peer_mac)
{
    if (peer_mac == NULL) return;
    size_t length = strlen(peer_mac);
    if (length >= sizeof(g_bt_hfp_ag.snapshot.last_peer_mac)) {
        length = sizeof(g_bt_hfp_ag.snapshot.last_peer_mac) - 1U;
    }
    memcpy(g_bt_hfp_ag.snapshot.last_peer_mac, peer_mac, length);
    g_bt_hfp_ag.snapshot.last_peer_mac[length] = '\0';
}

static void drain_completion(void)
{
    if (g_bt_hfp_ag.completion != NULL) {
        (void)platform_binary_sem_take(g_bt_hfp_ag.completion, 0U);
    }
}

static esp_err_t platform_register_callback(void)
{
#ifdef UNIT_TEST
    if (g_bt_hfp_ag.test_ops_set &&
        g_bt_hfp_ag.test_ops.register_callback != NULL) {
        return g_bt_hfp_ag.test_ops.register_callback();
    }
#endif
#ifdef ESP_PLATFORM
    return bt_hfp_ag_register_platform_callback();
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t platform_profile_init(void)
{
#ifdef UNIT_TEST
    if (g_bt_hfp_ag.test_ops_set &&
        g_bt_hfp_ag.test_ops.profile_init != NULL) {
        return g_bt_hfp_ag.test_ops.profile_init();
    }
#endif
#ifdef ESP_PLATFORM
    return esp_hf_ag_init();
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t platform_profile_deinit(void)
{
#ifdef UNIT_TEST
    if (g_bt_hfp_ag.test_ops_set &&
        g_bt_hfp_ag.test_ops.profile_deinit != NULL) {
        return g_bt_hfp_ag.test_ops.profile_deinit();
    }
#endif
#ifdef ESP_PLATFORM
    return esp_hf_ag_deinit();
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t bt_hfp_ag_profile_init(uint32_t timeout_ms)
{
    if (timeout_ms == 0U) return ESP_ERR_INVALID_ARG;
    esp_err_t err = bt_hfp_ag_context_ensure();
    if (err != ESP_OK) return err;
    err = bt_hfp_ag_lock();
    if (err != ESP_OK) return err;
    if (g_bt_hfp_ag.snapshot.lifecycle !=
        BT_HFP_AG_LIFECYCLE_UNINITIALIZED) {
        return bt_hfp_ag_unlock(ESP_ERR_INVALID_STATE);
    }
    drain_completion();
    g_bt_hfp_ag.snapshot.lifecycle = BT_HFP_AG_LIFECYCLE_INIT_PENDING;
    g_bt_hfp_ag.snapshot.profile_init_request_accepted = false;
    g_bt_hfp_ag.snapshot.last_error = ESP_OK;
    g_bt_hfp_ag.completion_result = ESP_ERR_INVALID_STATE;
    err = bt_hfp_ag_unlock(ESP_OK);
    if (err != ESP_OK) return err;

    err = platform_register_callback();
    if (err != ESP_OK) {
        if (bt_hfp_ag_lock() == ESP_OK) {
            bt_hfp_ag_set_fault_locked(err);
            (void)bt_hfp_ag_unlock(ESP_OK);
        }
        (void)bt_duplex_set_hfp_profile_global_state(
            BT_HFP_PROFILE_FAULTED);
        return err;
    }
    if (bt_hfp_ag_lock() != ESP_OK) return ESP_FAIL;
    g_bt_hfp_ag.snapshot.callback_registered = true;
    /* Mark the request as potentially live before invoking the platform. A
     * synchronous completion callback is therefore able to observe the same
     * rollback contract. Immediate platform rejection clears this flag. */
    g_bt_hfp_ag.snapshot.profile_init_request_accepted = true;
    (void)bt_hfp_ag_unlock(ESP_OK);

    err = platform_profile_init();
    if (err != ESP_OK) {
        if (bt_hfp_ag_lock() == ESP_OK) {
            g_bt_hfp_ag.snapshot.profile_init_request_accepted = false;
            bt_hfp_ag_set_fault_locked(err);
            (void)bt_hfp_ag_unlock(ESP_OK);
        }
        (void)bt_duplex_set_hfp_profile_global_state(
            BT_HFP_PROFILE_FAULTED);
        return err;
    }

    err = platform_binary_sem_take(g_bt_hfp_ag.completion, timeout_ms);
    if (err != ESP_OK) {
        if (bt_hfp_ag_lock() == ESP_OK) {
            g_bt_hfp_ag.snapshot.init_timeouts++;
            bt_hfp_ag_set_fault_locked(err);
            (void)bt_hfp_ag_unlock(ESP_OK);
        }
        (void)bt_duplex_set_hfp_profile_global_state(
            BT_HFP_PROFILE_FAULTED);
        return err;
    }
    err = bt_hfp_ag_lock();
    if (err != ESP_OK) return err;
    esp_err_t completion = g_bt_hfp_ag.completion_result;
    err = bt_hfp_ag_unlock(completion);
    if (err != ESP_OK) return err;

    /* The HCI audio callback and bounded FD-10 control state are created only
     * after the asynchronous profile-init event confirms a usable AG profile.
     * Either failure is a real profile-init failure and drives manager rollback. */
    err = bt_hfp_audio_register_callback();
    if (err == ESP_OK) {
        err = bt_hfp_audio_control_init();
    }
    if (err != ESP_OK) {
        if (bt_hfp_ag_lock() == ESP_OK) {
            bt_hfp_ag_set_fault_locked(err);
            (void)bt_hfp_ag_unlock(ESP_OK);
        }
        (void)bt_duplex_set_hfp_profile_global_state(
            BT_HFP_PROFILE_FAULTED);
        return err;
    }
    return ESP_OK;
}

esp_err_t bt_hfp_ag_profile_deinit(uint32_t timeout_ms)
{
    if (timeout_ms == 0U) return ESP_ERR_INVALID_ARG;
    esp_err_t err = bt_hfp_ag_lock();
    if (err != ESP_OK) return err;
    if (!g_bt_hfp_ag.snapshot.profile_init_request_accepted ||
        g_bt_hfp_ag.snapshot.lifecycle ==
            BT_HFP_AG_LIFECYCLE_UNINITIALIZED ||
        g_bt_hfp_ag.snapshot.lifecycle ==
            BT_HFP_AG_LIFECYCLE_DEINIT_PENDING) {
        return bt_hfp_ag_unlock(ESP_ERR_INVALID_STATE);
    }
    drain_completion();
    g_bt_hfp_ag.snapshot.lifecycle = BT_HFP_AG_LIFECYCLE_DEINIT_PENDING;
    g_bt_hfp_ag.completion_result = ESP_ERR_INVALID_STATE;
    err = bt_hfp_ag_unlock(ESP_OK);
    if (err != ESP_OK) return err;

    /* Reject new callback data and release bounded control waiters before the
     * stack is asked to tear down HFP. No callback-owned state is freed here. */
    bt_hfp_audio_control_profile_stopping();

    err = platform_profile_deinit();
    if (err != ESP_OK) {
        if (bt_hfp_ag_lock() == ESP_OK) {
            bt_hfp_ag_set_fault_locked(err);
            (void)bt_hfp_ag_unlock(ESP_OK);
        }
        return err;
    }
    err = platform_binary_sem_take(g_bt_hfp_ag.completion, timeout_ms);
    if (err != ESP_OK) {
        if (bt_hfp_ag_lock() == ESP_OK) {
            g_bt_hfp_ag.snapshot.deinit_timeouts++;
            bt_hfp_ag_set_fault_locked(err);
            (void)bt_hfp_ag_unlock(ESP_OK);
        }
        return err;
    }
    err = bt_hfp_ag_lock();
    if (err != ESP_OK) return err;
    completion = g_bt_hfp_ag.completion_result;
    return bt_hfp_ag_unlock(completion);
}

void bt_hfp_ag_force_cleanup_after_stack_shutdown(void)
{
#ifdef ESP_PLATFORM
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        ESP_LOGE(TAG, "Refusing HFP cleanup while Bluedroid may still deliver callbacks");
        return;
    }
#endif

    esp_err_t connection_cleanup =
        bt_hfp_connection_cleanup_after_stack_shutdown();
    if (connection_cleanup != ESP_OK) {
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Refusing HFP AG cleanup after SLC operation cleanup failed: %s",
                 esp_err_to_name(connection_cleanup));
#endif
        return;
    }

    esp_err_t control_cleanup =
        bt_hfp_audio_control_cleanup_after_stack_shutdown();
    if (control_cleanup != ESP_OK) {
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Refusing HFP AG cleanup after audio control cleanup failed: %s",
                 esp_err_to_name(control_cleanup));
#endif
        return;
    }

    esp_err_t audio_cleanup = bt_hfp_audio_cleanup_after_stack_shutdown();
    if (audio_cleanup != ESP_OK) {
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "Refusing HFP AG cleanup after audio callback cleanup failed: %s",
                 esp_err_to_name(audio_cleanup));
#endif
        return;
    }

    if (!g_bt_hfp_ag.resources_ready) return;
    platform_mutex_t lock = g_bt_hfp_ag.lock;
    platform_binary_sem_t completion_sem = g_bt_hfp_ag.completion;
    memset(&g_bt_hfp_ag, 0, sizeof(g_bt_hfp_ag));
    platform_binary_sem_delete(completion_sem);
    platform_mutex_delete(lock);
}

esp_err_t bt_hfp_ag_get_snapshot(bt_hfp_ag_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = bt_hfp_ag_lock();
    if (err != ESP_OK) return err;
    memcpy(out, &g_bt_hfp_ag.snapshot, sizeof(*out));
    return bt_hfp_ag_unlock(ESP_OK);
}

void bt_hfp_ag_handle_profile_result(bt_hfp_ag_profile_result_t result)
{
    bt_hfp_ag_lifecycle_t expected;
    bt_hfp_profile_state_t target = BT_HFP_PROFILE_FAULTED;
    bool success = false;
    bool signal = false;

    if (bt_hfp_ag_lock() != ESP_OK) return;
    g_bt_hfp_ag.snapshot.profile_events++;
    expected = g_bt_hfp_ag.snapshot.lifecycle;
    switch (result) {
    case BT_HFP_AG_PROFILE_INIT_SUCCESS:
    case BT_HFP_AG_PROFILE_INIT_ALREADY:
        signal = expected == BT_HFP_AG_LIFECYCLE_INIT_PENDING;
        success = true;
        target = BT_HFP_PROFILE_DISCONNECTED;
        break;
    case BT_HFP_AG_PROFILE_INIT_FAILED:
        signal = expected == BT_HFP_AG_LIFECYCLE_INIT_PENDING;
        target = BT_HFP_PROFILE_FAULTED;
        break;
    case BT_HFP_AG_PROFILE_DEINIT_SUCCESS:
    case BT_HFP_AG_PROFILE_DEINIT_ALREADY:
        signal = expected == BT_HFP_AG_LIFECYCLE_DEINIT_PENDING;
        success = true;
        target = BT_HFP_PROFILE_UNINITIALIZED;
        break;
    case BT_HFP_AG_PROFILE_DEINIT_FAILED:
        signal = expected == BT_HFP_AG_LIFECYCLE_DEINIT_PENDING;
        target = BT_HFP_PROFILE_FAULTED;
        break;
    default:
        g_bt_hfp_ag.snapshot.invalid_events++;
        break;
    }
    if (!signal && result <= BT_HFP_AG_PROFILE_DEINIT_FAILED) {
        g_bt_hfp_ag.snapshot.unhandled_events++;
    }
    (void)bt_hfp_ag_unlock(ESP_OK);
    if (!signal) return;

    completion = success
        ? bt_duplex_set_hfp_profile_global_state(target)
        : ESP_FAIL;
    if (!success && target == BT_HFP_PROFILE_FAULTED) {
        (void)bt_duplex_set_hfp_profile_global_state(target);
    }

    if (bt_hfp_ag_lock() != ESP_OK) return;
    if (g_bt_hfp_ag.snapshot.lifecycle != expected) {
        g_bt_hfp_ag.snapshot.unhandled_events++;
        (void)bt_hfp_ag_unlock(ESP_OK);
        return;
    }
    if (completion == ESP_OK) {
        g_bt_hfp_ag.snapshot.lifecycle =
            target == BT_HFP_PROFILE_DISCONNECTED
                ? BT_HFP_AG_LIFECYCLE_READY
                : BT_HFP_AG_LIFECYCLE_UNINITIALIZED;
        g_bt_hfp_ag.snapshot.profile_ready =
            target == BT_HFP_PROFILE_DISCONNECTED;
        if (target == BT_HFP_PROFILE_UNINITIALIZED) {
            g_bt_hfp_ag.snapshot.profile_init_request_accepted = false;
        }
        g_bt_hfp_ag.snapshot.last_error = ESP_OK;
    } else {
        bt_hfp_ag_set_fault_locked(completion);
    }
    g_bt_hfp_ag.completion_result = completion;
    (void)bt_hfp_ag_unlock(ESP_OK);
    (void)platform_binary_sem_give(g_bt_hfp_ag.completion);
}

#ifdef UNIT_TEST
esp_err_t bt_hfp_ag_test_set_platform_ops(
    const bt_hfp_ag_platform_ops_t *ops)
{
    if (ops == NULL || ops->register_callback == NULL ||
        ops->profile_init == NULL || ops->profile_deinit == NULL ||
        ops->unknown_at_error == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = bt_hfp_ag_context_ensure();
    if (err != ESP_OK) return err;
    g_bt_hfp_ag.test_ops = *ops;
    g_bt_hfp_ag.test_ops_set = true;
    return ESP_OK;
}

void bt_hfp_ag_test_reset_platform_ops(void)
{
    if (!g_bt_hfp_ag.resources_ready) return;
    memset(&g_bt_hfp_ag.test_ops, 0, sizeof(g_bt_hfp_ag.test_ops));
    g_bt_hfp_ag.test_ops_set = false;
}
#endif
