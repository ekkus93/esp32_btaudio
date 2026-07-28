#include "bt_hfp_connection.h"

#include <ctype.h>
#include <string.h>

#include "bt_app_core.h"
#include "bt_duplex_state.h"
#include "bt_manager_internal.h"
#include "platform_sync.h"
#include "platform_timing.h"
#include "util_safe.h"

#ifdef ESP_PLATFORM
#include "esp_bt_main.h"
#include "esp_hf_ag_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#else
#include "esp_log.h"
#endif

#define TAG "BT_HFP_CONN"
#define BT_HFP_WORK_EVENT 0x7001U

typedef struct {
    uint32_t serial;
} bt_hfp_work_request_t;

typedef struct {
    bool initialized;
    platform_mutex_t lock;
    platform_binary_sem_t request_done;
#ifdef ESP_PLATFORM
    esp_timer_handle_t watchdog;
#endif
    bt_hfp_connection_snapshot_t snapshot;
#ifdef UNIT_TEST
    bool test_ops_set;
    bt_hfp_connection_platform_ops_t test_ops;
#endif
} bt_hfp_connection_context_t;

static bt_hfp_connection_context_t s_connection;

static bool same_mac(const char *lhs, const char *rhs)
{
    if (lhs == NULL || rhs == NULL) return false;
    for (size_t i = 0; i < BT_HFP_AG_MAC_STR_LEN - 1U; ++i) {
        if (tolower((unsigned char)lhs[i]) !=
            tolower((unsigned char)rhs[i])) {
            return false;
        }
    }
    return lhs[BT_HFP_AG_MAC_STR_LEN - 1U] == '\0' &&
           rhs[BT_HFP_AG_MAC_STR_LEN - 1U] == '\0';
}

static esp_err_t connection_lock(void)
{
    if (!s_connection.initialized || s_connection.lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return platform_mutex_lock(s_connection.lock, PLATFORM_WAIT_FOREVER);
}

static esp_err_t connection_unlock(esp_err_t prior)
{
    esp_err_t err = platform_mutex_unlock(s_connection.lock);
    return prior != ESP_OK ? prior : err;
}

static void drain_request_completion(void)
{
    if (s_connection.request_done != NULL) {
        (void)platform_binary_sem_take(s_connection.request_done, 0U);
    }
}

static esp_err_t platform_slc_connect(esp_bd_addr_t remote_bda)
{
#ifdef UNIT_TEST
    if (s_connection.test_ops_set &&
        s_connection.test_ops.slc_connect != NULL) {
        return s_connection.test_ops.slc_connect(remote_bda);
    }
#endif
#ifdef ESP_PLATFORM
    return esp_hf_ag_slc_connect(remote_bda);
#else
    (void)remote_bda;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t platform_slc_disconnect(esp_bd_addr_t remote_bda)
{
#ifdef UNIT_TEST
    if (s_connection.test_ops_set &&
        s_connection.test_ops.slc_disconnect != NULL) {
        return s_connection.test_ops.slc_disconnect(remote_bda);
    }
#endif
#ifdef ESP_PLATFORM
    return esp_hf_ag_slc_disconnect(remote_bda);
#else
    (void)remote_bda;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static void cancel_watchdog(void)
{
#ifdef ESP_PLATFORM
    if (s_connection.watchdog != NULL) {
        esp_err_t err = esp_timer_stop(s_connection.watchdog);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to stop HFP SLC watchdog: %s",
                     esp_err_to_name(err));
        }
    }
#endif
}

static void mark_watchdog_timeout(void)
{
    uint32_t generation = 0U;
    char peer[BT_HFP_AG_MAC_STR_LEN] = {0};

    if (connection_lock() != ESP_OK) return;
    if (s_connection.snapshot.state != BT_HFP_OPERATION_REQUEST_SENT ||
        !s_connection.snapshot.pending) {
        (void)connection_unlock(ESP_OK);
        return;
    }

    s_connection.snapshot.state = BT_HFP_OPERATION_TIMED_OUT;
    s_connection.snapshot.pending = false;
    s_connection.snapshot.last_error = ESP_ERR_TIMEOUT;
    s_connection.snapshot.watchdog_timeouts++;
    generation = s_connection.snapshot.generation;
    util_safe_copy_str(peer, sizeof(peer), s_connection.snapshot.peer_mac);
    (void)connection_unlock(ESP_OK);

    (void)bt_duplex_set_health(generation, peer,
                               BT_AUDIO_HEALTH_DEGRADED,
                               ESP_ERR_TIMEOUT,
                               "HFP SLC operation timed out");
}

#ifdef ESP_PLATFORM
static void watchdog_callback(void *arg)
{
    (void)arg;
    mark_watchdog_timeout();
}

static esp_err_t arm_watchdog(void)
{
    if (s_connection.watchdog == NULL) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_timer_stop(s_connection.watchdog);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    return esp_timer_start_once(
        s_connection.watchdog,
        (uint64_t)BT_HFP_CONNECTION_WATCHDOG_TIMEOUT_MS * 1000ULL);
}
#else
static esp_err_t arm_watchdog(void)
{
    return ESP_OK;
}
#endif

esp_err_t bt_hfp_connection_init(void)
{
    if (s_connection.initialized) return ESP_OK;

    platform_mutex_t lock = platform_mutex_create();
    if (lock == NULL) return ESP_ERR_NO_MEM;
    platform_binary_sem_t done = platform_binary_sem_create();
    if (done == NULL) {
        platform_mutex_delete(lock);
        return ESP_ERR_NO_MEM;
    }

#ifdef ESP_PLATFORM
    esp_timer_handle_t watchdog = NULL;
    const esp_timer_create_args_t args = {
        .callback = watchdog_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "hfp_slc_watchdog",
        .skip_unhandled_events = false,
    };
    esp_err_t err = esp_timer_create(&args, &watchdog);
    if (err != ESP_OK) {
        platform_binary_sem_delete(done);
        platform_mutex_delete(lock);
        return err;
    }
#endif

    memset(&s_connection, 0, sizeof(s_connection));
    s_connection.lock = lock;
    s_connection.request_done = done;
#ifdef ESP_PLATFORM
    s_connection.watchdog = watchdog;
#endif
    s_connection.snapshot.type = BT_HFP_OPERATION_NONE;
    s_connection.snapshot.state = BT_HFP_OPERATION_IDLE;
    s_connection.snapshot.immediate_result = ESP_OK;
    s_connection.snapshot.last_error = ESP_OK;
    s_connection.initialized = true;
    return ESP_OK;
}

esp_err_t bt_hfp_connection_cleanup_after_stack_shutdown(void)
{
    if (!s_connection.initialized) return ESP_OK;
#ifdef ESP_PLATFORM
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }
#endif

    cancel_watchdog();
#ifdef ESP_PLATFORM
    if (s_connection.watchdog != NULL) {
        esp_err_t err = esp_timer_delete(s_connection.watchdog);
        if (err != ESP_OK) return err;
    }
#endif
    platform_mutex_t lock = s_connection.lock;
    platform_binary_sem_t done = s_connection.request_done;
    memset(&s_connection, 0, sizeof(s_connection));
    platform_binary_sem_delete(done);
    platform_mutex_delete(lock);
    return ESP_OK;
}

esp_err_t bt_hfp_connection_get_snapshot(bt_hfp_connection_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = connection_lock();
    if (err != ESP_OK) return err;
    memcpy(out, &s_connection.snapshot, sizeof(*out));
    return connection_unlock(ESP_OK);
}

static esp_err_t sync_or_create_duplex_session(const char *peer_mac,
                                                uint32_t *generation_out)
{
    bt_duplex_snapshot_t duplex;
    esp_err_t err = bt_duplex_get_snapshot(&duplex);
    if (err != ESP_OK) return err;

    if (!duplex.peer_valid) {
        err = bt_duplex_session_begin(peer_mac, BT_DUPLEX_MODE_AUTO,
                                      generation_out);
        if (err != ESP_OK) return err;
        err = bt_duplex_set_a2dp_profile_state(
            *generation_out, peer_mac, BT_A2DP_PROFILE_CONNECTING);
        if (err == ESP_OK) {
            err = bt_duplex_set_a2dp_profile_state(
                *generation_out, peer_mac, BT_A2DP_PROFILE_CONNECTED);
        }
        return err;
    }

    if (!same_mac(peer_mac, duplex.peer_mac)) {
        (void)bt_duplex_set_hfp_profile_state(
            duplex.session_generation, peer_mac,
            BT_HFP_PROFILE_CONNECTING);
        return ESP_ERR_INVALID_STATE;
    }

    *generation_out = duplex.session_generation;
    return ESP_OK;
}

static esp_err_t snapshot_active_manager_peer(
    char peer_out[BT_HFP_AG_MAC_STR_LEN])
{
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    bool initialized = bt_ctx.initialized;
    bool connected = bt_ctx.connected;
    util_safe_copy_str(peer_out, BT_HFP_AG_MAC_STR_LEN, bt_ctx.connected_mac);
    bt_ctx_unlock();

    if (!initialized || !connected || peer_out[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void rollback_profile_state(bt_hfp_operation_type_t type,
                                   uint32_t generation,
                                   const char *peer)
{
    bt_hfp_profile_state_t target =
        type == BT_HFP_OPERATION_CONNECT
            ? BT_HFP_PROFILE_DISCONNECTED
            : BT_HFP_PROFILE_SLC_CONNECTED;
    (void)bt_duplex_set_hfp_profile_state(generation, peer, target);
}

static void work_handler(uint16_t event, void *param)
{
    if (event != BT_HFP_WORK_EVENT || param == NULL) return;
    const bt_hfp_work_request_t *request =
        (const bt_hfp_work_request_t *)param;

    bt_hfp_operation_type_t type;
    esp_bd_addr_t bda = {0};
    uint32_t generation;
    char peer[BT_HFP_AG_MAC_STR_LEN] = {0};

    if (connection_lock() != ESP_OK) return;
    if (request->serial != s_connection.snapshot.serial ||
        s_connection.snapshot.state != BT_HFP_OPERATION_QUEUED ||
        !s_connection.snapshot.pending) {
        (void)connection_unlock(ESP_OK);
        return;
    }
    type = s_connection.snapshot.type;
    generation = s_connection.snapshot.generation;
    util_safe_copy_str(peer, sizeof(peer), s_connection.snapshot.peer_mac);
    if (!util_parse_mac(peer, bda)) {
        s_connection.snapshot.state = BT_HFP_OPERATION_REJECTED;
        s_connection.snapshot.pending = false;
        s_connection.snapshot.immediate_result = ESP_ERR_INVALID_ARG;
        s_connection.snapshot.last_error = ESP_ERR_INVALID_ARG;
        s_connection.snapshot.immediate_failures++;
        (void)connection_unlock(ESP_OK);
        rollback_profile_state(type, generation, peer);
        (void)platform_binary_sem_give(s_connection.request_done);
        return;
    }
    s_connection.snapshot.state = BT_HFP_OPERATION_REQUEST_SENT;
    (void)connection_unlock(ESP_OK);

    esp_err_t result = type == BT_HFP_OPERATION_CONNECT
        ? platform_slc_connect(bda)
        : platform_slc_disconnect(bda);

    bool should_arm = false;
    if (connection_lock() == ESP_OK) {
        if (request->serial == s_connection.snapshot.serial) {
            s_connection.snapshot.immediate_result = result;
            s_connection.snapshot.last_error = result;
            if (result != ESP_OK) {
                s_connection.snapshot.state = BT_HFP_OPERATION_REJECTED;
                s_connection.snapshot.pending = false;
                s_connection.snapshot.immediate_failures++;
            } else if (s_connection.snapshot.state ==
                       BT_HFP_OPERATION_REQUEST_SENT) {
                should_arm = true;
                s_connection.snapshot.deadline_ms =
                    platform_get_time_ms() +
                    BT_HFP_CONNECTION_WATCHDOG_TIMEOUT_MS;
            }
        }
        (void)connection_unlock(ESP_OK);
    }

    if (result != ESP_OK) {
        rollback_profile_state(type, generation, peer);
    } else if (should_arm) {
        esp_err_t timer_err = arm_watchdog();
        if (timer_err != ESP_OK && connection_lock() == ESP_OK) {
            if (request->serial == s_connection.snapshot.serial &&
                s_connection.snapshot.state ==
                    BT_HFP_OPERATION_REQUEST_SENT) {
                s_connection.snapshot.state = BT_HFP_OPERATION_TIMED_OUT;
                s_connection.snapshot.pending = false;
                s_connection.snapshot.immediate_result = timer_err;
                s_connection.snapshot.last_error = timer_err;
                s_connection.snapshot.watchdog_timeouts++;
            }
            (void)connection_unlock(ESP_OK);
            (void)bt_duplex_set_health(generation, peer,
                                       BT_AUDIO_HEALTH_FAULTED,
                                       timer_err,
                                       "Failed to arm HFP SLC watchdog");
        }
    }
    (void)platform_binary_sem_give(s_connection.request_done);
}

static esp_err_t begin_operation(bt_hfp_operation_type_t type,
                                 const char *requested_peer)
{
    esp_err_t err = bt_hfp_connection_init();
    if (err != ESP_OK) return err;

    bt_hfp_ag_snapshot_t profile;
    err = bt_hfp_ag_get_snapshot(&profile);
    if (err != ESP_OK || !profile.profile_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    char active_peer[BT_HFP_AG_MAC_STR_LEN] = {0};
    err = snapshot_active_manager_peer(active_peer);
    if (err != ESP_OK) return err;

    esp_bd_addr_t parsed = {0};
    const char *peer = requested_peer;
    if (type == BT_HFP_OPERATION_CONNECT) {
        if (peer == NULL || !util_parse_mac(peer, parsed)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (!same_mac(peer, active_peer)) {
            return ESP_ERR_INVALID_STATE;
        }
    } else {
        peer = active_peer;
        if (!util_parse_mac(peer, parsed)) return ESP_ERR_INVALID_STATE;
    }

    uint32_t generation = 0U;
    err = sync_or_create_duplex_session(peer, &generation);
    if (err != ESP_OK) return err;

    bt_duplex_snapshot_t duplex;
    err = bt_duplex_get_snapshot(&duplex);
    if (err != ESP_OK) return err;

    if (type == BT_HFP_OPERATION_CONNECT &&
        duplex.hfp_profile_state == BT_HFP_PROFILE_SLC_CONNECTED) {
        return ESP_OK;
    }
    if (type == BT_HFP_OPERATION_DISCONNECT &&
        duplex.hfp_profile_state == BT_HFP_PROFILE_DISCONNECTED) {
        return ESP_OK;
    }

    bt_hfp_profile_state_t requested_state =
        type == BT_HFP_OPERATION_CONNECT
            ? BT_HFP_PROFILE_CONNECTING
            : BT_HFP_PROFILE_DISCONNECTING;

    bt_hfp_work_request_t request;
    if (connection_lock() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_connection.snapshot.pending ||
        s_connection.snapshot.state == BT_HFP_OPERATION_TIMED_OUT) {
        (void)connection_unlock(ESP_OK);
        return ESP_ERR_INVALID_STATE;
    }

    /* Reserve the operation slot before changing authoritative state. */
    err = bt_duplex_set_hfp_profile_state(generation, peer, requested_state);
    if (err != ESP_OK) {
        (void)connection_unlock(ESP_OK);
        return err;
    }

    drain_request_completion();
    s_connection.snapshot.serial++;
    if (s_connection.snapshot.serial == 0U) {
        s_connection.snapshot.serial = 1U;
    }
    request.serial = s_connection.snapshot.serial;
    s_connection.snapshot.type = type;
    s_connection.snapshot.state = BT_HFP_OPERATION_QUEUED;
    s_connection.snapshot.pending = true;
    s_connection.snapshot.generation = generation;
    util_safe_copy_str(s_connection.snapshot.peer_mac,
                       sizeof(s_connection.snapshot.peer_mac), peer);
    s_connection.snapshot.deadline_ms = 0U;
    s_connection.snapshot.immediate_result = ESP_ERR_INVALID_STATE;
    s_connection.snapshot.last_error = ESP_OK;
    if (type == BT_HFP_OPERATION_CONNECT) {
        s_connection.snapshot.accepted_connect_requests++;
    } else {
        s_connection.snapshot.accepted_disconnect_requests++;
    }
    (void)connection_unlock(ESP_OK);

    if (!bt_app_work_dispatch(work_handler, BT_HFP_WORK_EVENT,
                              &request, sizeof(request), NULL)) {
        if (connection_lock() == ESP_OK) {
            if (request.serial == s_connection.snapshot.serial) {
                s_connection.snapshot.state = BT_HFP_OPERATION_REJECTED;
                s_connection.snapshot.pending = false;
                s_connection.snapshot.last_error = ESP_ERR_NO_MEM;
                s_connection.snapshot.dispatch_failures++;
            }
            (void)connection_unlock(ESP_OK);
        }
        rollback_profile_state(type, generation, peer);
        return ESP_ERR_NO_MEM;
    }

    err = platform_binary_sem_take(
        s_connection.request_done,
        BT_HFP_CONNECTION_REQUEST_TIMEOUT_MS);
    if (err != ESP_OK) {
        if (connection_lock() == ESP_OK) {
            if (request.serial == s_connection.snapshot.serial &&
                s_connection.snapshot.pending) {
                s_connection.snapshot.state = BT_HFP_OPERATION_TIMED_OUT;
                s_connection.snapshot.pending = false;
                s_connection.snapshot.last_error = err;
                s_connection.snapshot.watchdog_timeouts++;
            }
            (void)connection_unlock(ESP_OK);
        }
        (void)bt_duplex_set_health(generation, peer,
                                   BT_AUDIO_HEALTH_DEGRADED,
                                   err,
                                   "BtAppTask HFP request timed out");
        return err;
    }

    esp_err_t immediate = ESP_FAIL;
    if (connection_lock() == ESP_OK) {
        if (request.serial == s_connection.snapshot.serial) {
            immediate = s_connection.snapshot.immediate_result;
        }
        (void)connection_unlock(ESP_OK);
    }
    return immediate;
}

esp_err_t bt_hfp_connect(const char *mac)
{
    return begin_operation(BT_HFP_OPERATION_CONNECT, mac);
}

esp_err_t bt_hfp_disconnect(void)
{
    return begin_operation(BT_HFP_OPERATION_DISCONNECT, NULL);
}

static bt_hfp_profile_state_t map_profile_state(
    bt_hfp_ag_connection_state_t state)
{
    switch (state) {
    case BT_HFP_AG_CONNECTION_DISCONNECTED:
        return BT_HFP_PROFILE_DISCONNECTED;
    case BT_HFP_AG_CONNECTION_CONNECTING:
    case BT_HFP_AG_CONNECTION_RFCOMM_CONNECTED:
        return BT_HFP_PROFILE_CONNECTING;
    case BT_HFP_AG_CONNECTION_SLC_CONNECTED:
        return BT_HFP_PROFILE_SLC_CONNECTED;
    case BT_HFP_AG_CONNECTION_DISCONNECTING:
        return BT_HFP_PROFILE_DISCONNECTING;
    default:
        return BT_HFP_PROFILE_STATE_COUNT;
    }
}

esp_err_t bt_hfp_connection_handle_event(
    const char *peer_mac,
    bt_hfp_ag_connection_state_t state)
{
    if (!s_connection.initialized) return ESP_ERR_NOT_FOUND;
    bt_hfp_profile_state_t mapped = map_profile_state(state);
    if (mapped == BT_HFP_PROFILE_STATE_COUNT || peer_mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bt_hfp_connection_snapshot_t operation;
    if (bt_hfp_connection_get_snapshot(&operation) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (operation.type == BT_HFP_OPERATION_NONE ||
        (operation.state != BT_HFP_OPERATION_QUEUED &&
         operation.state != BT_HFP_OPERATION_REQUEST_SENT &&
         operation.state != BT_HFP_OPERATION_TIMED_OUT)) {
        return ESP_ERR_NOT_FOUND;
    }

    if (!same_mac(peer_mac, operation.peer_mac)) {
        if (connection_lock() == ESP_OK) {
            s_connection.snapshot.wrong_peer_events++;
            (void)connection_unlock(ESP_OK);
        }
        (void)bt_duplex_set_hfp_profile_state(
            operation.generation, peer_mac, mapped);
        return ESP_OK;
    }

    bt_duplex_snapshot_t duplex;
    if (bt_duplex_get_snapshot(&duplex) != ESP_OK ||
        operation.generation != duplex.session_generation ||
        operation.state == BT_HFP_OPERATION_TIMED_OUT) {
        if (connection_lock() == ESP_OK) {
            s_connection.snapshot.stale_operation_events++;
            (void)connection_unlock(ESP_OK);
        }
        (void)bt_duplex_record_stale_operation_event(
            operation.generation, operation.peer_mac);
        return ESP_OK;
    }

    bool complete = false;
    bool rejected = false;
    if (operation.type == BT_HFP_OPERATION_CONNECT) {
        complete = state == BT_HFP_AG_CONNECTION_SLC_CONNECTED;
        rejected = state == BT_HFP_AG_CONNECTION_DISCONNECTED;
    } else {
        complete = state == BT_HFP_AG_CONNECTION_DISCONNECTED;
    }

    if (complete || rejected) {
        cancel_watchdog();
        if (connection_lock() == ESP_OK) {
            if (operation.serial == s_connection.snapshot.serial) {
                s_connection.snapshot.pending = false;
                s_connection.snapshot.state = complete
                    ? BT_HFP_OPERATION_CONFIRMED
                    : BT_HFP_OPERATION_REJECTED;
                s_connection.snapshot.last_error =
                    complete ? ESP_OK : ESP_FAIL;
                if (rejected) {
                    s_connection.snapshot.remote_rejections++;
                }
            }
            (void)connection_unlock(ESP_OK);
        }
    }

    (void)bt_duplex_set_hfp_profile_state(
        operation.generation, operation.peer_mac, mapped);
    return ESP_OK;
}

#ifdef UNIT_TEST
esp_err_t bt_hfp_connection_test_set_platform_ops(
    const bt_hfp_connection_platform_ops_t *ops)
{
    if (ops == NULL || ops->slc_connect == NULL ||
        ops->slc_disconnect == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = bt_hfp_connection_init();
    if (err != ESP_OK) return err;
    s_connection.test_ops = *ops;
    s_connection.test_ops_set = true;
    return ESP_OK;
}

void bt_hfp_connection_test_expire_watchdog(void)
{
    mark_watchdog_timeout();
}
#endif
