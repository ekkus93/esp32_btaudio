#include "bt_events_avrc.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"

#define TAG "BT_EVT_AVRC"

// Minimal AVRCP controller callback: log connection state and ignore others
void bt_events_avrc_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    if (param == NULL) {
        return;
    }

    switch (event) {
        case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
            /* IDF provides 'connected' bool in conn_stat; avoid accessing
             * non-existent fields on older/newer headers. */
            bool connected = false;
#if defined(ESP_AVRC_CT_CONNECTION_STATE_EVT)
            connected = param->conn_stat.connected;
#endif
            ESP_LOGI(TAG, "AVRCP connection state: %d", connected ? 1 : 0);  // NOLINT(bugprone-branch-clone)
            break;
        }
        case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:  // NOLINT(bugprone-branch-clone)
            ESP_LOGD(TAG, "AVRCP passthrough rsp key=%d state=%d", param->psth_rsp.key_code, param->psth_rsp.key_state);  // NOLINT(bugprone-branch-clone)
            break;
        case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
            ESP_LOGD(TAG, "AVRCP remote features: 0x%x", (unsigned int)param->rmt_feats.feat_mask);  // NOLINT(bugprone-branch-clone)
            break;
        default:
            ESP_LOGD(TAG, "AVRCP event: %d", event);  // NOLINT(bugprone-branch-clone)
            break;
    }
}

#elif defined(UNIT_TEST)

// Stub implementation for unit tests - just accepts events without crashing
void bt_events_avrc_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param) {
    (void)event;
    (void)param;
    // No-op for unit tests
}

#endif // ESP_PLATFORM
