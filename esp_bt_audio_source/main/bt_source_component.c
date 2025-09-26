#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "bt_app_core.h"
#include "bt_source.h"

static const char *TAG = "BT_SOURCE";

/* Forward declarations for external components */
extern void bt_connection_manager_init(esp_a2d_cb_t conn_cb, esp_a2d_source_data_cb_t audio_cb);
extern void bt_streaming_manager_init(void);
extern void bt_connection_state_handler(esp_a2d_connection_state_t state, esp_bd_addr_t bd_addr);
extern void bt_audio_state_handler(esp_a2d_audio_state_t state, esp_bd_addr_t bd_addr);

/* Static function declarations */
static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);
static void bt_app_av_sm_hdlr(uint16_t event, void *param);
static void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);

/* Initialize Bluetooth classic */
static esp_err_t bt_classic_init(void)
{
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Initializing Bluetooth Classic");
    
    /* Initialize Bluetooth controller */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Enable Bluetooth controller */
    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Initialize Bluedroid stack */
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Enable Bluedroid stack */
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Set device name */
    ret = esp_bt_dev_set_device_name("ESP32-A2DP-Source");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set device name failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Set discoverable and connectable mode */
    ret = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set scan mode failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Initialize AVRCP controller */
    ret = esp_avrc_ct_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AVRCP controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    esp_avrc_ct_register_callback(bt_app_rc_ct_cb);
    
    /* Initialize A2DP source */
    ret = esp_a2d_source_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "A2DP source init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    esp_a2d_register_callback(bt_app_a2d_cb);
    
    /* Initialize Bluetooth application task */
    bt_app_task_start_up();
    
    /* Initialize connection and streaming managers */
    bt_connection_manager_init(bt_app_a2d_cb, NULL);
    bt_streaming_manager_init();
    
    ESP_LOGI(TAG, "Bluetooth Classic initialized");
    return ESP_OK;
}

/* A2DP callback */
static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    ESP_LOGD(TAG, "A2DP event: %d", event);
    
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            ESP_LOGI(TAG, "A2DP connection state: %d", param->conn_stat.state);
            bt_app_work_dispatch(bt_app_av_sm_hdlr, event, param, sizeof(esp_a2d_cb_param_t), NULL);
            break;
            
        case ESP_A2D_AUDIO_STATE_EVT:
            ESP_LOGI(TAG, "A2DP audio state: %d", param->audio_stat.state);
            bt_app_work_dispatch(bt_app_av_sm_hdlr, event, param, sizeof(esp_a2d_cb_param_t), NULL);
            break;
            
        case ESP_A2D_AUDIO_CFG_EVT:
            ESP_LOGI(TAG, "A2DP audio config: %d", param->audio_cfg.mcc.type);
            /* Do any audio configuration here if needed */
            break;
            
        default:
            ESP_LOGW(TAG, "Unhandled A2DP event: %d", event);
            break;
    }
}

/* Main state machine handler */
static void bt_app_av_sm_hdlr(uint16_t event, void *param)
{
    esp_a2d_cb_param_t *a2d = (esp_a2d_cb_param_t *)param;
    
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            /* Forward to connection manager */
            bt_connection_state_handler(a2d->conn_stat.state, a2d->conn_stat.remote_bda);
            break;
            
        case ESP_A2D_AUDIO_STATE_EVT:
            /* Forward to audio manager */
            bt_audio_state_handler(a2d->audio_stat.state, a2d->audio_stat.remote_bda);
            break;
            
        default:
            ESP_LOGW(TAG, "Unhandled event in state machine: %d", event);
            break;
    }
}

/* AVRCP controller callback */
static void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    ESP_LOGD(TAG, "AVRCP controller event: %d", event);
    
    switch (event) {
        case ESP_AVRC_CT_CONNECTION_STATE_EVT:
            ESP_LOGI(TAG, "AVRCP connection state: %d", param->conn_stat.connected);
            /* Handle AVRCP connection state */
            break;
            
        case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
            ESP_LOGI(TAG, "AVRCP passthrough response: %d", param->psth_rsp.key_code);
            /* Handle AVRCP passthrough response */
            break;
            
        case ESP_AVRC_CT_METADATA_RSP_EVT:
            ESP_LOGI(TAG, "AVRCP metadata response");
            /* Handle AVRCP metadata (song info, etc.) */
            break;
            
        case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
            ESP_LOGI(TAG, "AVRCP change notification: %d", param->change_ntf.event_id);
            /* Handle AVRCP notifications (play state changes, etc.) */
            break;
            
        default:
            ESP_LOGW(TAG, "Unhandled AVRCP controller event: %d", event);
            break;
    }
}

/* Public API implementations */

esp_err_t bt_init(void)
{
    return bt_classic_init();
}

esp_err_t bt_deinit(void)
{
    esp_err_t ret;
    
    /* Shutdown A2DP */
    ret = esp_a2d_source_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "A2DP source deinit failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Shutdown AVRCP */
    ret = esp_avrc_ct_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AVRCP controller deinit failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Shutdown Bluetooth task */
    bt_app_task_shut_down();
    
    /* Disable and deinitialize Bluetooth */
    ret = esp_bluedroid_disable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid disable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_bluedroid_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid deinit failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_bt_controller_disable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller disable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_bt_controller_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller deinit failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Bluetooth deinitialized");
    return ESP_OK;
}

esp_err_t bt_connect(const char* addr)
{
    if (addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Convert address string to ESP format */
    esp_bd_addr_t bda;
    if (sscanf(addr, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
               &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) != ESP_BD_ADDR_LEN) {
        ESP_LOGE(TAG, "Invalid address format: %s", addr);
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Connect to device */
    esp_err_t ret = esp_a2d_source_connect(bda);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to device: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Connecting to device: %s", addr);
    return ESP_OK;
}

esp_err_t bt_disconnect(void)
{
    uint8_t remote_bda[ESP_BD_ADDR_LEN] = {0};
    /* Since we need to provide an address, use zeros if we don't have a specific one */
    
    /* Disconnect from device */
    esp_err_t ret = esp_a2d_source_disconnect(remote_bda);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disconnect: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Disconnecting from device");
    return ESP_OK;
}
