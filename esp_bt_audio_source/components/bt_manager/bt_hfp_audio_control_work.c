#include "bt_hfp_audio_control_internal.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "bt_app_core.h"

#ifdef ESP_PLATFORM
#include "esp_hf_ag_api.h"
#endif

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    value = (char)tolower((unsigned char)value);
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

bool parse_mac(const char *text, esp_bd_addr_t out)
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

bool same_peer(const char *lhs, const char *rhs)
{
    return lhs != NULL && rhs != NULL && strlen(lhs) == 17U &&
           strlen(rhs) == 17U && strcasecmp(lhs, rhs) == 0;
}

void copy_peer(char out[BT_DUPLEX_MAC_STR_LEN], const char *peer)
{
    size_t length = peer == NULL ? 0U : strlen(peer);
    if (length >= BT_DUPLEX_MAC_STR_LEN) length = BT_DUPLEX_MAC_STR_LEN - 1U;
    if (length > 0U) memcpy(out, peer, length);
    out[length] = '\0';
}

esp_err_t platform_audio_connect(esp_bd_addr_t remote_bda)
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

esp_err_t platform_audio_disconnect(esp_bd_addr_t remote_bda)
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

void dispatch_remote_cleanup(uint32_t generation, const char *peer_mac,
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

esp_err_t queue_lower_request(uint32_t serial,
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

esp_err_t issue_cleanup_disconnect(uint32_t serial)
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

bool lower_request_may_be_live(uint32_t serial)
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

esp_err_t rollback_started_i2s(uint32_t serial, uint32_t generation,
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
