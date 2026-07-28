#include "bt_hfp_audio.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "bt_app_core.h"
#include "hfp_i2s_output.h"
#include "platform_sync.h"

#ifdef ESP_PLATFORM
#include "esp_hf_ag_api.h"
#endif

#define BT_HFP_AUDIO_WORK_EVENT 0x7002U
#define BT_HFP_AUDIO_REMOTE_CLEANUP_EVENT 0x7003U
#define BT_HFP_AUDIO_FALLBACK_I2S_STOP_TIMEOUT_MS 1000U

typedef struct {
    uint32_t serial;
    bt_hfp_audio_operation_type_t type;
    bool cleanup_only;
} bt_hfp_audio_work_request_t;

typedef struct {
    uint32_t generation;
    bool disconnect_lower;
    bool stop_i2s;
    char peer_mac[BT_DUPLEX_MAC_STR_LEN];
} bt_hfp_audio_remote_cleanup_t;

typedef struct {
    bool initialized;
    platform_mutex_t lock;
    platform_binary_sem_t request_done;
    platform_binary_sem_t event_done;
    bt_hfp_audio_control_snapshot_t snapshot;
} bt_hfp_audio_control_context_t;

static bt_hfp_audio_control_context_t s_control;

#ifdef UNIT_TEST
static bt_hfp_audio_control_platform_ops_t s_test_ops;
static bool s_test_ops_set;
#endif

static void stop_i2s_for_session(uint32_t generation, const char *peer_mac,
                                 esp_err_t *result_out);

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    value = (char)tolower((unsigned char)value);
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static bool parse_mac(const char *text, esp_bd_addr_t out)
{
    if (text == NULL || out == NULL || strlen(text) != 17U) return false;
    for (size_t index = 0; index < 6U; ++index) {
        const size_t offset = index * 3U;
        const int high = hex_value(text[offset]);
        const int low = hex_value(text[offset + 1U]);
        if (high < 0 || low < 0) return false;
        if (index < 5U && text[offset + 2U] != ':') return false;
        out[index] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static bool same_peer(const char *lhs, const char *rhs)
{
    return lhs != NULL && rhs != NULL && strlen(lhs) == 17U &&
           strlen(rhs) == 17U && strcasecmp(lhs, rhs) == 0;
}

static void copy_peer(char out[BT_DUPLEX_MAC_STR_LEN], const char *peer)
{
    size_t length = peer == NULL ? 0U : strlen(peer);
    if (length >= BT_DUPLEX_MAC_STR_LEN) length = BT_DUPLEX_MAC_STR_LEN - 1U;
    if (length > 0U) memcpy(out, peer, length);
    out[length] = '\0';
}

static esp_err_t control_lock(void)
{
    if (!s_control.initialized || s_control.lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return platform_mutex_lock(s_control.lock, PLATFORM_WAIT_FOREVER);
}

static esp_err_t control_unlock(esp_err_t prior)
{
    const esp_err_t err = platform_mutex_unlock(s_control.lock);
    return prior != ESP_OK ? prior : err;
}

static void drain_sem(platform_binary_sem_t sem)
{
    if (sem != NULL) (void)platform_binary_sem_take(sem, 0U);
}

static esp_err_t context_ensure(void)
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

static esp_err_t platform_audio_connect(esp_bd_addr_t remote_bda)
{
#ifdef UNIT_TEST
    if (s_test_ops_set && s_test_ops.audio_connect != NULL) {
        return s_test_ops.audio_connect(remote_bda);
    }
#endif
#ifdef ESP_PLATFORM
    return esp_hf_ag_audio_connect(remote_bda);
#else
    (void)remote_bda;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t platform_audio_disconnect(esp_bd_addr_t remote_bda)
{
#ifdef UNIT_TEST
    if (s_test_ops_set && s_test_ops.audio_disconnect != NULL) {
        return s_test_ops.audio_disconnect(remote_bda);
    }
#endif
#ifdef ESP_PLATFORM
    return esp_hf_ag_audio_disconnect(remote_bda);
#else
    (void)remote_bda;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

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

static esp_err_t set_audio_state_if_needed(uint32_t generation,
                                           const char *peer_mac,
                                           bt_hfp_audio_state_t state)
{
    bt_duplex_snapshot_t duplex;
    esp_err_t err = bt_duplex_get_snapshot(&duplex);
    if (err != ESP_OK) return err;
    if (duplex.hfp_audio_state == state) return ESP_OK;
    return bt_duplex_set_hfp_audio_state(generation, peer_mac, state);
}

static void set_health(uint32_t generation, const char *peer_mac,
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

static esp_err_t start_i2s_for_session(uint32_t generation,
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
        if (control_lock() == ESP_OK) {
            s_control.snapshot.i2s_start_failures++;
            s_control.snapshot.last_error = sync_err != ESP_OK ? sync_err : err;
            (void)control_unlock(ESP_OK);
        }
        if (sync_err == ESP_OK &&
            local.state == HFP_I2S_OUTPUT_QUARANTINED) {
            set_health(generation, peer_mac, BT_AUDIO_HEALTH_QUARANTINED,
                       err, "HFP I2S startup rollback quarantined");
        } else {
            set_health(generation, peer_mac, BT_AUDIO_HEALTH_FAULTED,
                       sync_err != ESP_OK ? sync_err : err,
                       "HFP I2S startup failed");
        }
        return sync_err != ESP_OK ? sync_err : err;
    }

    return set_i2s_state_if_needed(generation, peer_mac, BT_HFP_I2S_RUNNING);
}

static void record_i2s_stop_failure(uint32_t generation, const char *peer_mac,
                                    esp_err_t result,
                                    const hfp_i2s_output_snapshot_t *after,
                                    bool snapshot_valid)
{
    if (control_lock() == ESP_OK) {
        s_control.snapshot.i2s_stop_failures++;
        s_control.snapshot.last_error = result;
        (void)control_unlock(ESP_OK);
    }
    if (snapshot_valid && after != NULL &&
        after->state == HFP_I2S_OUTPUT_QUARANTINED) {
        set_health(generation, peer_mac, BT_AUDIO_HEALTH_QUARANTINED,
                   result, "HFP I2S stop quarantined");
    } else {
        set_health(generation, peer_mac, BT_AUDIO_HEALTH_FAULTED,
                   result, "HFP I2S stop failed");
    }
}

static void stop_i2s_for_session(uint32_t generation, const char *peer_mac,
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
    result = sync_err != ESP_OK ? sync_err : err;
    if (result != ESP_OK) {
        record_i2s_stop_failure(generation, peer_mac, result, &after,
                                sync_err == ESP_OK);
    }

done:
    if (result_out != NULL) *result_out = result;
}

static void remote_cleanup_handler(uint16_t event, void *param)
{
    if (event != BT_HFP_AUDIO_REMOTE_CLEANUP_EVENT || param == NULL) return;
    const bt_hfp_audio_remote_cleanup_t *cleanup =
        (const bt_hfp_audio_remote_cleanup_t *)param;

    esp_err_t disconnect_result = ESP_OK;
    if (cleanup->disconnect_lower) {
        esp_bd_addr_t bda = {0};
        disconnect_result = parse_mac(cleanup->peer_mac, bda)
            ? platform_audio_disconnect(bda)
            : ESP_ERR_INVALID_ARG;
        if (disconnect_result != ESP_OK && cleanup->generation != 0U) {
            set_health(cleanup->generation, cleanup->peer_mac,
                       BT_AUDIO_HEALTH_FAULTED, disconnect_result,
                       "Failed to reject unexpected HFP audio link");
        }
    }

    if (cleanup->stop_i2s && cleanup->generation != 0U) {
        esp_err_t stop_result = ESP_OK;
        stop_i2s_for_session(cleanup->generation, cleanup->peer_mac,
                             &stop_result);
    }
}

static void dispatch_remote_cleanup(uint32_t generation, const char *peer_mac,
                                    bool disconnect_lower, bool stop_i2s)
{
    bt_hfp_audio_remote_cleanup_t cleanup = {
        .generation = generation,
        .disconnect_lower = disconnect_lower,
        .stop_i2s = stop_i2s,
    };
    copy_peer(cleanup.peer_mac, peer_mac);
    if (!bt_app_work_dispatch(remote_cleanup_handler,
                              BT_HFP_AUDIO_REMOTE_CLEANUP_EVENT,
                              &cleanup, (int)sizeof(cleanup), NULL) &&
        generation != 0U) {
        set_health(generation, peer_mac, BT_AUDIO_HEALTH_FAULTED,
                   ESP_ERR_NO_MEM,
                   "Failed to dispatch HFP remote audio cleanup");
    }
}

static void work_handler(uint16_t event, void *param)
{
    if (event != BT_HFP_AUDIO_WORK_EVENT || param == NULL) return;
    const bt_hfp_audio_work_request_t *request =
        (const bt_hfp_audio_work_request_t *)param;

    char peer[BT_DUPLEX_MAC_STR_LEN] = {0};
    if (control_lock() != ESP_OK) return;
    if (request->serial != s_control.snapshot.serial) {
        (void)control_unlock(ESP_OK);
        return;
    }
    copy_peer(peer, s_control.snapshot.peer_mac);
    if (!request->cleanup_only) {
        if (!s_control.snapshot.pending ||
            s_control.snapshot.state != BT_HFP_AUDIO_OPERATION_QUEUED ||
            request->type != s_control.snapshot.type) {
            (void)control_unlock(ESP_OK);
            return;
        }
        s_control.snapshot.state = BT_HFP_AUDIO_OPERATION_REQUEST_SENT;
    }
    (void)control_unlock(ESP_OK);

    esp_bd_addr_t bda = {0};
    const esp_err_t result = parse_mac(peer, bda)
        ? (request->type == BT_HFP_AUDIO_OPERATION_START
               ? platform_audio_connect(bda)
               : platform_audio_disconnect(bda))
        : ESP_ERR_INVALID_ARG;

    if (control_lock() == ESP_OK) {
        if (request->serial == s_control.snapshot.serial) {
            if (request->cleanup_only) {
                s_control.snapshot.cleanup_result = result;
                if (result != ESP_OK) {
                    s_control.snapshot.cleanup_disconnect_failures++;
                }
            } else {
                s_control.snapshot.immediate_result = result;
                s_control.snapshot.lower_request_accepted = result == ESP_OK;
                if (result != ESP_OK &&
                    (s_control.snapshot.state ==
                         BT_HFP_AUDIO_OPERATION_REQUEST_SENT ||
                     s_control.snapshot.state ==
                         BT_HFP_AUDIO_OPERATION_QUEUED)) {
                    s_control.snapshot.state = BT_HFP_AUDIO_OPERATION_REJECTED;
                    s_control.snapshot.pending = false;
                    s_control.snapshot.immediate_failures++;
                    s_control.snapshot.last_error = result;
                }
            }
        }
        (void)control_unlock(ESP_OK);
    }
    (void)platform_binary_sem_give(s_control.request_done);
}

static esp_err_t reserve_operation(bt_hfp_audio_operation_type_t type,
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

static void update_operation_generation(uint32_t serial, uint32_t generation)
{
    if (control_lock() != ESP_OK) return;
    if (serial == s_control.snapshot.serial) {
        s_control.snapshot.generation = generation;
    }
    (void)control_unlock(ESP_OK);
}

static void finish_operation(uint32_t serial, esp_err_t result,
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

static esp_err_t queue_lower_request(uint32_t serial,
                                     bt_hfp_audio_operation_type_t type)
{
    bt_hfp_audio_work_request_t request = {
        .serial = serial,
        .type = type,
        .cleanup_only = false,
    };

    drain_sem(s_control.request_done);
    if (control_lock() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (serial != s_control.snapshot.serial ||
        !s_control.snapshot.api_active) {
        return control_unlock(ESP_ERR_INVALID_STATE);
    }
    s_control.snapshot.type = type;
    s_control.snapshot.state = BT_HFP_AUDIO_OPERATION_QUEUED;
    s_control.snapshot.pending = true;
    s_control.snapshot.immediate_result = ESP_ERR_INVALID_STATE;
    s_control.snapshot.completion_result = ESP_ERR_INVALID_STATE;
    s_control.snapshot.lower_request_accepted = false;
    (void)control_unlock(ESP_OK);

    if (!bt_app_work_dispatch(work_handler, BT_HFP_AUDIO_WORK_EVENT,
                              &request, (int)sizeof(request), NULL)) {
        if (control_lock() == ESP_OK) {
            if (serial == s_control.snapshot.serial) {
                s_control.snapshot.state = BT_HFP_AUDIO_OPERATION_REJECTED;
                s_control.snapshot.pending = false;
                s_control.snapshot.dispatch_failures++;
                s_control.snapshot.last_error = ESP_ERR_NO_MEM;
            }
            (void)control_unlock(ESP_OK);
        }
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = platform_binary_sem_take(
        s_control.request_done, BT_HFP_AUDIO_REQUEST_TIMEOUT_MS);
    if (err != ESP_OK) {
        if (control_lock() == ESP_OK) {
            if (serial == s_control.snapshot.serial &&
                s_control.snapshot.pending) {
                s_control.snapshot.state = BT_HFP_AUDIO_OPERATION_TIMED_OUT;
                s_control.snapshot.pending = false;
                s_control.snapshot.request_timeouts++;
                s_control.snapshot.last_error = err;
            }
            (void)control_unlock(ESP_OK);
        }
        return err;
    }

    esp_err_t immediate = ESP_FAIL;
    if (control_lock() == ESP_OK) {
        if (serial == s_control.snapshot.serial) {
            immediate = s_control.snapshot.immediate_result;
            if (immediate == ESP_OK &&
                s_control.snapshot.state ==
                    BT_HFP_AUDIO_OPERATION_REQUEST_SENT) {
                s_control.snapshot.state = BT_HFP_AUDIO_OPERATION_WAITING_EVENT;
            }
        }
        (void)control_unlock(ESP_OK);
    }
    return immediate;
}

static esp_err_t issue_cleanup_disconnect(uint32_t serial)
{
    bt_hfp_audio_work_request_t request = {
        .serial = serial,
        .type = BT_HFP_AUDIO_OPERATION_STOP,
        .cleanup_only = true,
    };

    drain_sem(s_control.request_done);
    if (control_lock() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (serial != s_control.snapshot.serial) {
        return control_unlock(ESP_ERR_INVALID_STATE);
    }
    s_control.snapshot.cleanup_disconnect_requests++;
    s_control.snapshot.cleanup_result = ESP_ERR_INVALID_STATE;
    (void)control_unlock(ESP_OK);

    if (!bt_app_work_dispatch(work_handler, BT_HFP_AUDIO_WORK_EVENT,
                              &request, (int)sizeof(request), NULL)) {
        if (control_lock() == ESP_OK) {
            s_control.snapshot.cleanup_disconnect_failures++;
            s_control.snapshot.cleanup_result = ESP_ERR_NO_MEM;
            (void)control_unlock(ESP_OK);
        }
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = platform_binary_sem_take(
        s_control.request_done, BT_HFP_AUDIO_REQUEST_TIMEOUT_MS);
    if (err != ESP_OK) {
        if (control_lock() == ESP_OK) {
            s_control.snapshot.cleanup_disconnect_failures++;
            s_control.snapshot.cleanup_result = err;
            (void)control_unlock(ESP_OK);
        }
        return err;
    }

    esp_err_t result = ESP_FAIL;
    if (control_lock() == ESP_OK) {
        result = s_control.snapshot.cleanup_result;
        (void)control_unlock(ESP_OK);
    }
    return result;
}

static bool lower_request_may_be_live(uint32_t serial)
{
    bool live = false;
    if (control_lock() == ESP_OK) {
        if (serial == s_control.snapshot.serial) {
            live = s_control.snapshot.lower_request_accepted ||
                   s_control.snapshot.state ==
                       BT_HFP_AUDIO_OPERATION_TIMED_OUT;
        }
        (void)control_unlock(ESP_OK);
    }
    return live;
}

static esp_err_t rollback_started_i2s(uint32_t serial, uint32_t generation,
                                      const char *peer_mac,
                                      esp_err_t cause,
                                      bool disconnect_lower)
{
    if (control_lock() == ESP_OK) {
        if (serial == s_control.snapshot.serial) {
            s_control.snapshot.rollback_attempts++;
        }
        (void)control_unlock(ESP_OK);
    }

    bt_hfp_audio_profile_stopping();
    bt_duplex_snapshot_t duplex;
    if (bt_duplex_get_snapshot(&duplex) == ESP_OK &&
        duplex.hfp_audio_state == BT_HFP_AUDIO_CONNECTING) {
        (void)set_audio_state_if_needed(
            generation, peer_mac, BT_HFP_AUDIO_DISCONNECTED);
    }

    esp_err_t disconnect_result = ESP_OK;
    if (disconnect_lower) {
        disconnect_result = issue_cleanup_disconnect(serial);
    }

    esp_err_t stop_result = ESP_OK;
    stop_i2s_for_session(generation, peer_mac, &stop_result);
    const esp_err_t result = stop_result != ESP_OK
        ? stop_result
        : (disconnect_result != ESP_OK ? disconnect_result : cause);

    if (result != cause && control_lock() == ESP_OK) {
        s_control.snapshot.rollback_failures++;
        s_control.snapshot.last_error = result;
        (void)control_unlock(ESP_OK);
    }
    return result;
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
