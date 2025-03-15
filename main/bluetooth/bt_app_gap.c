/**
 * @file bt_app_gap.c
 * @brief Implementation of Bluetooth Generic Access Profile (GAP) events handler
 *
 * This module implements the main event handler for Bluetooth GAP events including
 * device discovery, authentication/pairing events, and other Bluetooth management events.
 * It's responsible for processing discovered devices, handling authentication procedures,
 * and maintaining connection security parameters.
 */
#include "bluetooth/bt_app_gap.h"
#include "bluetooth/bt_app_global.h"
#include "custom_log.h"

static const char *TAG = "BT_APP_GAP";

/**
 * @brief Process Bluetooth GAP events
 *
 * Central event handler for all Bluetooth GAP events. Processes events such as
 * device discovery results, authentication completions, PIN/SSP requests,
 * and various connection-related events.
 * 
 * @param event The GAP event type being reported
 * @param param Parameters specific to the event type
 */
void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    SAFE_ESP_LOGD(TAG, "GAP event handler called with event: %d", event);
    char addr_str[18]; // Buffer for Bluetooth address string representation
    
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            // New device discovered during scan
            SAFE_ESP_LOGD(TAG, "DISC_RES: num_prop=%d", param->disc_res.num_prop);
            ESP_LOG_BUFFER_HEX(TAG, param->disc_res.bda, ESP_BD_ADDR_LEN);
            
            // Extract device name from Extended Inquiry Response (EIR) data
            uint8_t eir_length = 0;
            uint8_t *eir_name = NULL;

            for (int i = 0; i < param->disc_res.num_prop; i++) {
                esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
                if (p->type == ESP_BT_GAP_DEV_PROP_EIR) {
                    // Look for complete local name in EIR data
                    eir_name = esp_bt_gap_resolve_eir_data((uint8_t *)p->val, 
                                                          ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, 
                                                          &eir_length);
                    break;
                }
            }

            // Use extracted name or "Unknown" as fallback
            if (eir_name) {
                strncpy(addr_str, (char *)eir_name, eir_length);
                addr_str[eir_length] = '\0';
            } else {
                strcpy(addr_str, "Unknown");
            }

            // Check if we've already discovered this device in current scan
            bool device_found = false;
            for (int i = 0; i < num_discovered_devices; i++) {
                if (memcmp(discovered_devices[i].bda, param->disc_res.bda, ESP_BD_ADDR_LEN) == 0) {
                    device_found = true;
                    break;
                }
            }

            // Add to discovered devices list if new and if there's space
            if (!device_found && num_discovered_devices < MAX_DEVICES) {
                // Store device address and name
                memcpy(discovered_devices[num_discovered_devices].bda, 
                       param->disc_res.bda, ESP_BD_ADDR_LEN);
                strncpy(discovered_devices[num_discovered_devices].name, 
                        addr_str, ESP_BT_GAP_MAX_BDNAME_LEN);
                discovered_devices[num_discovered_devices].name[ESP_BT_GAP_MAX_BDNAME_LEN] = '\0';
                num_discovered_devices++;
            }

            // Log discovered device details
            SAFE_ESP_LOGI(TAG, "Device found: %02x:%02x:%02x:%02x:%02x:%02x, Name: %s",
                     param->disc_res.bda[0], param->disc_res.bda[1], param->disc_res.bda[2],
                     param->disc_res.bda[3], param->disc_res.bda[4], param->disc_res.bda[5], 
                     addr_str);
            break;
        }
        
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            // Discovery state changes (started/stopped)
            SAFE_ESP_LOGD(TAG, "DISC_STATE_CHANGED: state=%d", param->disc_st_chg.state);
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                // When discovery completes, summarize results
                SAFE_ESP_LOGI(TAG, "Discovery stopped.");
                SAFE_ESP_LOGI(TAG, "Discovered devices:");
                
                // List all discovered devices
                for (int i = 0; i < num_discovered_devices; i++) {
                    SAFE_ESP_LOGI(TAG, "MAC: %02x:%02x:%02x:%02x:%02x:%02x, Name: %s",
                             discovered_devices[i].bda[0], discovered_devices[i].bda[1], 
                             discovered_devices[i].bda[2], discovered_devices[i].bda[3], 
                             discovered_devices[i].bda[4], discovered_devices[i].bda[5],
                             discovered_devices[i].name);
                }
                
                // Reset discovery state for next scan
                num_discovered_devices = 0;
                memset(discovered_devices, 0, sizeof(discovered_devices));
            }
            break;

        case ESP_BT_GAP_AUTH_CMPL_EVT:
            // Authentication completed (success or failure)
            SAFE_ESP_LOGD(TAG, "AUTH_CMPL: status=%d", param->auth_cmpl.stat);
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                // Authentication succeeded
                SAFE_ESP_LOGI(TAG, "Authentication success: %s", param->auth_cmpl.device_name);
                
                // Stop any pairing retries since we succeeded
                s_pairing_in_progress = false;
                if (s_pairing_retry_timer != NULL && xTimerIsTimerActive(s_pairing_retry_timer)) {
                    xTimerStop(s_pairing_retry_timer, 0);
                }
            } else {
                // Authentication failed
                SAFE_ESP_LOGE(TAG, "Authentication failed, status: %d", param->auth_cmpl.stat);
                s_pairing_in_progress = false;
                
                // Stop any pairing retries on failure
                if (s_pairing_retry_timer != NULL && xTimerIsTimerActive(s_pairing_retry_timer)) {
                    xTimerStop(s_pairing_retry_timer, 0);
                }
                
                // Reset connection state if we were still connecting
                if (s_a2d_state == APP_AV_STATE_CONNECTING) {
                    s_a2d_state = APP_AV_STATE_UNCONNECTED;
                    
                    // Add a small delay to let the stack recover
                    vTaskDelay(pdMS_TO_TICKS(500));
                    
                    // Clear the L2CAP congestion flag
                    s_l2cap_congestion_flag = false;
                }
            }
            break;

        case ESP_BT_GAP_PIN_REQ_EVT:
            // PIN code request for legacy (pre-SSP) pairing
            SAFE_ESP_LOGI(TAG, "PIN request event received");
            if (!pin_required) {
                // No PIN required - use empty PIN for Just Works pairing
                SAFE_ESP_LOGI(TAG, "No PIN required for this device");
                esp_bt_pin_code_t pin_code = {0};
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 0, pin_code);
            } else {
                // PIN required - use default PIN '0000'
                SAFE_ESP_LOGI(TAG, "Using default PIN '0000' for pairing");
                esp_bt_pin_code_t pin_code = {'0', '0', '0', '0'};
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
            }
            break;

        case ESP_BT_GAP_CFM_REQ_EVT:
            // SSP (Secure Simple Pairing) confirmation request
            SAFE_ESP_LOGI(TAG, "SSP confirmation request: auto-confirming.");
            // Automatically confirm without user interaction
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;

        // Handle ACL events more explicitly for debugging earbuds issues
        case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
            {
                uint8_t *bda = param->acl_conn_cmpl_stat.bda;
                SAFE_ESP_LOGI(TAG, "ACL connection complete: status=%d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                     param->acl_conn_cmpl_stat.stat, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
                
                // Give the stack some time to process ACL queue
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            break;
            
        case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
            {
                uint8_t *bda = param->acl_disconn_cmpl_stat.bda;
                SAFE_ESP_LOGI(TAG, "ACL disconnection complete: reason=%d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                     param->acl_disconn_cmpl_stat.reason, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
                
                // Clear any congestion flags when ACL is disconnected
                s_l2cap_congestion_flag = false;
            }
            break;
            
        case ESP_BT_GAP_MODE_CHG_EVT:
            SAFE_ESP_LOGI(TAG, "Mode change event: mode=%d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                 param->mode_chg.mode, param->mode_chg.bda[0], param->mode_chg.bda[1],
                 param->mode_chg.bda[2], param->mode_chg.bda[3], param->mode_chg.bda[4],
                 param->mode_chg.bda[5]);
            break;

        // Log but don't handle in detail: ACL connection events and other common events
        case ESP_BT_GAP_KEY_NOTIF_EVT:   // SSP passkey notification
        case ESP_BT_GAP_KEY_REQ_EVT:     // SSP passkey request
        case ESP_BT_GAP_READ_RSSI_DELTA_EVT:  // RSSI reading result
        case ESP_BT_GAP_CONFIG_EIR_DATA_EVT:  // EIR config result
        case ESP_BT_GAP_SET_AFH_CHANNELS_EVT: // AFH channels result
        case ESP_BT_GAP_READ_REMOTE_NAME_EVT: // Remote name result
        case ESP_BT_GAP_REMOVE_BOND_DEV_COMPLETE_EVT: // Unbonding result
        case ESP_BT_GAP_QOS_CMPL_EVT:         // QoS complete
            SAFE_ESP_LOGI(TAG, "ACL event received, ignoring pairing fallback.");
            break;

        // Known but unhandled events - just log them
        case ESP_BT_GAP_SET_PAGE_TO_EVT:  // Page timeout changed
        case ESP_BT_GAP_GET_PAGE_TO_EVT:  // Page timeout read
        case ESP_BT_GAP_ACL_PKT_TYPE_CHANGED_EVT: // ACL packet type changed
        case ESP_BT_GAP_ENC_CHG_EVT:      // Encryption change
        case ESP_BT_GAP_SET_MIN_ENC_KEY_SIZE_EVT: // Min encryption key size
        case ESP_BT_GAP_GET_DEV_NAME_CMPL_EVT:    // Get device name completed
            SAFE_ESP_LOGI(TAG, "Unhandled GAP event: %d", event);
            break;

        // Default case for any other unexpected events
        default:
            SAFE_ESP_LOGD(TAG, "Unhandled GAP event %d, raw data:", event);
            // Log raw event data for debugging purposes
            ESP_LOG_BUFFER_HEX(TAG, (uint8_t *)param, sizeof(*param));
            break;
    }
}