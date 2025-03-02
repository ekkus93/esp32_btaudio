#include "bluetooth/bt_app_conn.h"
#include "bluetooth/bt_app_global.h"

#include "bluetooth/bt_app_a2dp.h"
#include "bluetooth/bt_app_av.h"
#include "custom_log.h"

#define TAG "BT_APP_CONN"

// Create a new timer task to periodically check for memory issues
static void memory_monitor_task(void *arg) {
    while(1) {
        // Get free heap memory
        size_t free_heap = esp_get_free_heap_size();
        SAFE_ESP_LOGI(TAG, "Free heap: %u bytes", free_heap);
        
        // If memory is critically low, force congestion mode
        if (free_heap < 20000) { // 20KB is a critical threshold
            s_severe_congestion = true;
            s_last_congestion_time = (uint32_t)(esp_timer_get_time() / 1000);
            ESP_LOGW(TAG, "Memory critically low (%u bytes). Enforcing congestion control.", 
                   free_heap);
        }
        
        vTaskDelay(pdMS_TO_TICKS(MEMORY_CHECK_INTERVAL_MS));
    }
}

static discovered_device_t discovered_devices[MAX_DEVICES];
void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    SAFE_ESP_LOGD(TAG, "GAP event handler called with event: %d", event);
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            // Log raw device address and property count
            SAFE_ESP_LOGD(TAG, "DISC_RES: num_prop=%d", param->disc_res.num_prop);
            ESP_LOG_BUFFER_HEX(TAG, param->disc_res.bda, ESP_BD_ADDR_LEN);
            char bda_str[18];
            uint8_t eir_length = 0;
            uint8_t *eir_name = NULL;

            for (int i = 0; i < param->disc_res.num_prop; i++) {
                esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
                if (p->type == ESP_BT_GAP_DEV_PROP_EIR) {
                    eir_name = esp_bt_gap_resolve_eir_data((uint8_t *)p->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &eir_length);
                    break;
                }
            }

            if (eir_name) {
                strncpy(bda_str, (char *)eir_name, eir_length);
                bda_str[eir_length] = '\0';
            } else {
                strcpy(bda_str, "Unknown");
            }

            bool device_found = false;
            for (int i = 0; i < num_discovered_devices; i++) {
                if (memcmp(discovered_devices[i].bda, param->disc_res.bda, ESP_BD_ADDR_LEN) == 0) {
                    device_found = true;
                    break;
                }
            }

            if (!device_found && num_discovered_devices < MAX_DEVICES) {
                memcpy(discovered_devices[num_discovered_devices].bda, param->disc_res.bda, ESP_BD_ADDR_LEN);
                strncpy(discovered_devices[num_discovered_devices].name, bda_str, ESP_BT_GAP_MAX_BDNAME_LEN);
                discovered_devices[num_discovered_devices].name[ESP_BT_GAP_MAX_BDNAME_LEN] = '\0';
                num_discovered_devices++;
            }

            SAFE_ESP_LOGI(TAG, "Device found: %02x:%02x:%02x:%02x:%02x:%02x, Name: %s",
                     param->disc_res.bda[0], param->disc_res.bda[1], param->disc_res.bda[2],
                     param->disc_res.bda[3], param->disc_res.bda[4], param->disc_res.bda[5], bda_str);
            break;
        }
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            SAFE_ESP_LOGD(TAG, "DISC_STATE_CHANGED: state=%d", param->disc_st_chg.state);
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                SAFE_ESP_LOGI(TAG, "Discovery stopped.");
                SAFE_ESP_LOGI(TAG, "Discovered devices:");
                for (int i = 0; i < num_discovered_devices; i++) {
                    SAFE_ESP_LOGI(TAG, "MAC: %02x:%02x:%02x:%02x:%02x:%02x, Name: %s",
                             discovered_devices[i].bda[0], discovered_devices[i].bda[1], discovered_devices[i].bda[2],
                             discovered_devices[i].bda[3], discovered_devices[i].bda[4], discovered_devices[i].bda[5],
                             discovered_devices[i].name);
                }
                num_discovered_devices = 0;
                memset(discovered_devices, 0, sizeof(discovered_devices));
            }
            break;

        case ESP_BT_GAP_RMT_SRVCS_EVT:
            SAFE_ESP_LOGI(TAG, "Remote device services discovered");
            break;

        case ESP_BT_GAP_RMT_SRVC_REC_EVT:
            SAFE_ESP_LOGI(TAG, "Remote device service record");
            break;

        case ESP_BT_GAP_AUTH_CMPL_EVT:
            SAFE_ESP_LOGD(TAG, "AUTH_CMPL: status=%d", param->auth_cmpl.stat);
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                SAFE_ESP_LOGI(TAG, "Authentication success: %s", param->auth_cmpl.device_name);
                // ...existing success handling...
            } else {
                SAFE_ESP_LOGE(TAG, "Authentication failed, status: %d", param->auth_cmpl.stat);
            }
            break;

        case ESP_BT_GAP_PIN_REQ_EVT:
            SAFE_ESP_LOGI(TAG, "PIN request event received");
            // If no PIN required or fallback, always reply with empty PIN (legacy mode)
            if (!pin_required) {
                SAFE_ESP_LOGI(TAG, "Legacy pairing: replying with empty PIN");
                esp_bt_pin_code_t legacy_pin = {0};
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 0, legacy_pin);
            } else {
                SAFE_ESP_LOGI(TAG, "Legacy pairing with fixed PIN 1234");
                esp_bt_pin_code_t pin_code = { '1', '2', '3', '4' };
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
            }
            break;

        case ESP_BT_GAP_CFM_REQ_EVT:
            SAFE_ESP_LOGI(TAG, "SSP confirmation request: auto-confirming.");
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;

        case ESP_BT_GAP_KEY_NOTIF_EVT:
            // ...existing key notification code...
            break;

        case ESP_BT_GAP_KEY_REQ_EVT:
            // ...existing key request code...
            break;

        case ESP_BT_GAP_READ_RSSI_DELTA_EVT:
        case ESP_BT_GAP_CONFIG_EIR_DATA_EVT:
        case ESP_BT_GAP_SET_AFH_CHANNELS_EVT:
        case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
        case ESP_BT_GAP_MODE_CHG_EVT:
        case ESP_BT_GAP_REMOVE_BOND_DEV_COMPLETE_EVT:
        case ESP_BT_GAP_QOS_CMPL_EVT:
            SAFE_ESP_LOGI(TAG, "ACL event received, ignoring pairing fallback.");
            break;
        case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
            SAFE_ESP_LOGI(TAG, "ACL event received, ignoring pairing fallback.");
            break;
        case ESP_BT_GAP_SET_PAGE_TO_EVT:
        case ESP_BT_GAP_GET_PAGE_TO_EVT:
        case ESP_BT_GAP_ACL_PKT_TYPE_CHANGED_EVT:
        case ESP_BT_GAP_ENC_CHG_EVT:
        case ESP_BT_GAP_SET_MIN_ENC_KEY_SIZE_EVT:
        case ESP_BT_GAP_GET_DEV_NAME_CMPL_EVT:
            SAFE_ESP_LOGI(TAG, "Unhandled GAP event: %d", event);
            break;

        default:
            SAFE_ESP_LOGD(TAG, "Unhandled GAP event %d, raw data:", event);
            ESP_LOG_BUFFER_HEX(TAG, (uint8_t *)param, sizeof(*param));
            break;
    }
}

esp_err_t bluetooth_start_discovery(void) {
    SAFE_ESP_LOGI(TAG, "###Starting Bluetooth device discovery");
    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
        esp_err_t ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 30, 0);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to start discovery: %s", esp_err_to_name(ret));
        }
        xSemaphoreGive(s_bt_resource_mutex);
    }
    SAFE_ESP_LOGI(TAG, "###Leaving bluetooth_start_discovery");
    return ESP_OK;
}

// Add more detailed logging in the pairing function
esp_err_t bluetooth_pair_device(const char *mac_str, bool require_pin) {
    esp_bd_addr_t bd_addr;
    if (sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
               &bd_addr[0], &bd_addr[1], &bd_addr[2], 
               &bd_addr[3], &bd_addr[4], &bd_addr[5]) != 6) {
        SAFE_ESP_LOGE(TAG, "Invalid MAC address format");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(pending_pair_addr, bd_addr, ESP_BD_ADDR_LEN);
    pin_required = require_pin;

    SAFE_ESP_LOGD(TAG, "Pairing with MAC=%02x:%02x:%02x:%02x:%02x:%02x, require_pin=%s",
             bd_addr[0], bd_addr[1], bd_addr[2],
             bd_addr[3], bd_addr[4], bd_addr[5],
             require_pin ? "true" : "false");

    // Use an alternative security profile: change IO capability from ESP_BT_IO_CAP_IO to ESP_BT_IO_CAP_OUT
    esp_bt_io_cap_t io_cap = ESP_BT_IO_CAP_OUT; // Previously: ESP_BT_IO_CAP_IO
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io_cap, sizeof(uint8_t));

    s_a2d_state = APP_AV_STATE_CONNECTING;
    
    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
        // First establish connection without security
        esp_err_t ret = esp_a2d_source_connect(bd_addr);  // Changed to source
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to initiate connection: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }
        xSemaphoreGive(s_bt_resource_mutex);
    }

    return ESP_OK;
}

// Add Bluetooth initialization function
esp_err_t bluetooth_init(void) {
    esp_err_t ret;

    SAFE_ESP_LOGI(TAG, "###Initializing Bluetooth stack - 1");

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    SAFE_ESP_LOGI(TAG, "###Initializing Bluetooth stack - 2");

    // Initialize controller with increased stack size
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.controller_task_stack_size = 8192;  // Double the stack size (was 4096)
    bt_cfg.hci_uart_no = UART_NUM_1;  // Use UART1 instead of default to avoid conflicts
    
    // Increase the BT controller memory if available
    bt_cfg.bt_max_acl_conn = 1; // We only need one connection
    bt_cfg.bt_max_sync_conn = 0; // Not using SCO
    // Limit the number of advertising packets to save memory
    bt_cfg.normal_adv_size = 10;
    // Remove the line with normal_scan_size as it doesn't exist in the struct
    
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        SAFE_ESP_LOGE(TAG, "Bluetooth controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    SAFE_ESP_LOGI(TAG, "###Initializing Bluetooth stack - 3");

    ret = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    if (ret) {
        SAFE_ESP_LOGE(TAG, "Bluetooth controller enable failed: %s", esp_err_to_name(ret));
        esp_bt_controller_deinit();
        return ret;
    }
    SAFE_ESP_LOGI(TAG, "###Initializing Bluetooth stack - 4");

    ret = esp_bluedroid_init();
    if (ret) {
        SAFE_ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    SAFE_ESP_LOGI(TAG, "###Initializing Bluetooth stack - 5");

    ret = esp_bluedroid_enable();
    if (ret) {
        SAFE_ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    SAFE_ESP_LOGI(TAG, "###Initializing Bluetooth stack - 6");

    // 1. Initialize AVRCP controller first
    ret = esp_avrc_ct_init();
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "AVRCP controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. Register AVRCP callback
    ret = esp_avrc_ct_register_callback(avrc_ct_callback);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "AVRCP callback register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 3. Initialize AVRCP target
    ret = esp_avrc_tg_init();
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "AVRCP target init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 4. Configure AVRCP target features
    esp_avrc_rn_evt_cap_mask_t evt_set = {0};
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
    esp_avrc_tg_set_rn_evt_cap(&evt_set);

    // 5. Initialize A2DP source
    ret = esp_a2d_source_init();
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "A2DP source init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 6. Register A2DP callbacks
    esp_a2d_register_callback(bt_app_a2d_cb);
    esp_a2d_source_register_data_callback(a2dp_source_data_cb);

    // 7. Set device name
    ret = esp_bt_gap_set_device_name("ESP_A2DP_SRC");
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Set device name failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 8. Set Class of Device
    esp_bt_cod_t cod = {
        .major = 0x01,
        .minor = 0x03,
        .service = 0x24
    };
    ret = esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_ALL);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Set Class of Device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 9. Register GAP callback and set scan mode
    esp_bt_gap_register_callback(gap_event_handler);
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    // Remove L2CAP configuration
    /*
    // Configure L2CAP channel parameters
    esp_bt_cfg_l2cap_capab_ex_data_t l2cap_cfg;
    memset(&l2cap_cfg, 0, sizeof(esp_bt_cfg_l2cap_capab_ex_data_t));
    l2cap_cfg.l2cap_tx_buf_size = L2CAP_TX_BUF_SIZE;
    l2cap_cfg.l2cap_mtu        = L2CAP_MTU;
    esp_bt_gap_set_l2cap_capability(&l2cap_cfg);
    */

    if (!s_bt_resource_mutex) {
        s_bt_resource_mutex = xSemaphoreCreateMutex();
        if (!s_bt_resource_mutex) {
            SAFE_ESP_LOGE(TAG, "Failed to create Bluetooth resource mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    // Initialize the volume from NVS
    nvs_handle_t nvs_handle;
    ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret == ESP_OK) {
        // Check if volume exists in NVS
        uint8_t vol;
        ret = nvs_get_u8(nvs_handle, BT_VOLUME_KEY, &vol);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            // If not found, set default volume of 32
            nvs_set_u8(nvs_handle, BT_VOLUME_KEY, DEFAULT_VOLUME);
            nvs_commit(nvs_handle);
            s_current_volume = DEFAULT_VOLUME;
        } else if (ret == ESP_OK) {
            // Use the stored volume
            s_current_volume = vol;
        }
        nvs_close(nvs_handle);
    }

    // At the end of initialization, start our memory monitor task
    xTaskCreate(memory_monitor_task, "mem_monitor", 2048, NULL, 1, NULL);

    // Set default volume after initialization
    ret = bluetooth_set_volume(DEFAULT_VOLUME);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to set default volume: %s", esp_err_to_name(ret));
    } else {
        SAFE_ESP_LOGI(TAG, "Default volume set to: %d", DEFAULT_VOLUME);
    }
    
    SAFE_ESP_LOGI(TAG, "Bluetooth stack initialized successfully with enhanced congestion control");
    return ESP_OK;
}

// Function to disconnect from a paired device
esp_err_t bluetooth_disconnect_device(void) {
    if (s_a2d_state == APP_AV_STATE_CONNECTED) {
        if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
            SAFE_ESP_LOGI(TAG, "Attempting to disconnect from device...");
            esp_err_t ret = esp_a2d_source_disconnect(pending_pair_addr);
            if (ret != ESP_OK) {
                SAFE_ESP_LOGE(TAG, "Failed to disconnect: %s", esp_err_to_name(ret));
                xSemaphoreGive(s_bt_resource_mutex);
                return ret;
            }
            xSemaphoreGive(s_bt_resource_mutex);
        }
        SAFE_ESP_LOGI(TAG, "Disconnected from device");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "No device is currently connected");
        return ESP_ERR_INVALID_STATE;
    }
}

// Function to unpair a device
esp_err_t bluetooth_unpair_device(void) {
    if (s_a2d_state == APP_AV_STATE_CONNECTED || s_a2d_state == APP_AV_STATE_CONNECTING) {
        if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
            esp_err_t ret = esp_bt_gap_remove_bond_device(pending_pair_addr);
            if (ret != ESP_OK) {
                SAFE_ESP_LOGE(TAG, "Failed to unpair: %s", esp_err_to_name(ret));
                xSemaphoreGive(s_bt_resource_mutex);
                return ret;
            }
            xSemaphoreGive(s_bt_resource_mutex);
        }
        SAFE_ESP_LOGI(TAG, "Unpaired device");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "No device is currently paired");
        return ESP_ERR_INVALID_STATE;
    }
}

// Function to connect to a paired device
esp_err_t bluetooth_connect_device(const char *mac_str) {
    esp_bd_addr_t bd_addr;
    if (sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
               &bd_addr[0], &bd_addr[1], &bd_addr[2], 
               &bd_addr[3], &bd_addr[4], &bd_addr[5]) != 6) {
        SAFE_ESP_LOGE(TAG, "Invalid MAC address format");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(pending_pair_addr, bd_addr, ESP_BD_ADDR_LEN);

    SAFE_ESP_LOGD(TAG, "Connecting to MAC=%02x:%02x:%02x:%02x:%02x:%02x",
             bd_addr[0], bd_addr[1], bd_addr[2],
             bd_addr[3], bd_addr[4], bd_addr[5]);

    s_a2d_state = APP_AV_STATE_CONNECTING;
    
    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
        // Establish connection
        esp_err_t ret = esp_a2d_source_connect(bd_addr);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to initiate connection: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }
        xSemaphoreGive(s_bt_resource_mutex);
    }

    // Set default volume after connecting
    esp_err_t ret = bluetooth_set_volume(DEFAULT_VOLUME);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to set default volume after connecting: %s", esp_err_to_name(ret));
    } else {
        SAFE_ESP_LOGI(TAG, "Default volume set after connecting to: %d", DEFAULT_VOLUME);
    }

    return ESP_OK;
}

// Function to restart the Bluetooth stack
esp_err_t restart_bluetooth_stack(void) {
    esp_err_t ret;

    // Disable Bluedroid
    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
        ret = esp_bluedroid_disable();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to disable Bluedroid: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Deinitialize Bluedroid
        ret = esp_bluedroid_deinit();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to deinitialize Bluedroid: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Disable Bluetooth controller
        ret = esp_bt_controller_disable();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to disable Bluetooth controller: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Deinitialize Bluetooth controller
        ret = esp_bt_controller_deinit();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to deinitialize Bluetooth controller: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Reinitialize and enable Bluetooth stack
        ret = bluetooth_init();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to reinitialize Bluetooth stack: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }
        xSemaphoreGive(s_bt_resource_mutex);
    }

    SAFE_ESP_LOGI(TAG, "Bluetooth stack restarted successfully");
    return ESP_OK;
}

// Function to set the Bluetooth device name
esp_err_t bluetooth_set_device_name(const char *name) {
    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
        esp_err_t ret = esp_bt_gap_set_device_name(name);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to set device name: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Save the device name to NVS
        nvs_handle_t nvs_handle;
        ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        ret = nvs_set_str(nvs_handle, BT_DEVICE_NAME_KEY, name);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to save device name to NVS: %s", esp_err_to_name(ret));
            nvs_close(nvs_handle);
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        nvs_close(nvs_handle);
        SAFE_ESP_LOGI(TAG, "Device name set to: %s", name);

        xSemaphoreGive(s_bt_resource_mutex);
    }

    // Restart Bluetooth stack to apply the new name
    return restart_bluetooth_stack();
}

// Function to get the Bluetooth device name
esp_err_t bluetooth_get_device_name(char *name, size_t max_len) {
    // Read the device name from NVS
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t name_len = max_len;
    ret = nvs_get_str(nvs_handle, BT_DEVICE_NAME_KEY, name, &name_len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // If not found, use the default name and save it to NVS
        strncpy(name, DEFAULT_BT_DEVICE_NAME, max_len);
        name[max_len - 1] = '\0';  // Ensure null-termination
        ret = nvs_set_str(nvs_handle, BT_DEVICE_NAME_KEY, name);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to save default device name to NVS: %s", esp_err_to_name(ret));
        } else {
            ret = nvs_commit(nvs_handle);
            if (ret != ESP_OK) {
                SAFE_ESP_LOGE(TAG, "Failed to commit default device name to NVS: %s", esp_err_to_name(ret));
            }
        }
    } else if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to read device name from NVS: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    SAFE_ESP_LOGI(TAG, "Device name: %s", name);
    return ret;
}


