#include "bt_events_gap.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "bt_pairing_store.h"
#include "bt_scan.h"

#define TAG "BT_EVT_GAP"

// GAP callback for Bluetooth events
void bt_events_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            // Device discovery result
            bt_scan_handle_discovery_result(param->disc_res.bda, 
                                            param->disc_res.num_prop,
                                            param->disc_res.prop);
            break;
        }
        
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            // Discovery state changed
            bt_scan_handle_state_change(param->disc_st_chg.state);
            break;
            
        case ESP_BT_GAP_PIN_REQ_EVT: {
            // Remote device is requesting a PIN code (legacy pairing)
            bt_pairing_handle_pin_request(param->pin_req.bda);
            break;
        }

        case ESP_BT_GAP_CFM_REQ_EVT: {
            // SSP confirmation request (numeric comparison)
            bt_pairing_handle_ssp_confirm(param->cfm_req.bda, param->cfm_req.num_val);
            break;
        }

        case ESP_BT_GAP_AUTH_CMPL_EVT: {
            // Authentication complete (pairing result)
            bt_pairing_handle_auth_complete(param->auth_cmpl.bda, param->auth_cmpl.stat);
            break;
        }

        default:
            break;
    }
}

#elif defined(UNIT_TEST)

// Stub implementation for unit tests - just accepts events without crashing
void bt_events_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    (void)event;
    (void)param;
    // No-op for unit tests
}

#endif // ESP_PLATFORM
