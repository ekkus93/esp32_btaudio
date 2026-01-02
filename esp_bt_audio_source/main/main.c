/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdarg.h>
#include <math.h>  // For sinf()
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"

#if HEAP_MEMORY_DEBUG
#include "esp_heap_caps.h"
#include "osi/allocator.h"
#endif

/* Bluetooth includes */
#include "esp_bt.h"
#include "bt_app_core.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "command_interface.h"
#include "driver/uart.h"
#include "audio_processor.h"
#include "mem_util.h"
/* Needed for pin fallbacks and i2s port constants when auto-initializing audio */
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "nvs_storage.h"
#include "bt_manager.h"

static int safe_vsnprintf(char *dst, size_t dst_size, const char *fmt, va_list args) {
    if (dst == NULL || dst_size == 0 || fmt == NULL) {
        return 0;
    }
    int written = vsnprintf(dst, dst_size, fmt, args); // NOLINT(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    if (written < 0 || (size_t)written >= dst_size) {
        dst[dst_size - 1] = '\0';
    }
    return written;
}

static int safe_snprintf(char *dst, size_t dst_size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int written = safe_vsnprintf(dst, dst_size, fmt, args);
    va_end(args);
    return written;
}

/* log tags */
#define BT_AV_TAG             "BT_AV"
#define BT_RC_CT_TAG          "RC_CT"

/* device name */
#define LOCAL_DEVICE_NAME     "ESP_A2DP_SRC"

/* AVRCP used transaction label */
#define APP_RC_CT_TL_GET_CAPS            (0)
#define APP_RC_CT_TL_RN_VOLUME_CHANGE    (1)

enum {
    BT_APP_STACK_UP_EVT   = 0x0000,    /* event for stack up */
    BT_APP_HEART_BEAT_EVT = 0xff00,    /* event for heart beat */
};

/* Small FreeRTOS task that polls the command interface */
static void cmd_process_task(void* arg) {
    (void)arg;
    for (;;) {
        cmd_process();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* A2DP global states */
enum {
    APP_AV_STATE_IDLE,
    APP_AV_STATE_DISCOVERING,
    APP_AV_STATE_DISCOVERED,
    APP_AV_STATE_UNCONNECTED,
    APP_AV_STATE_CONNECTING,
    APP_AV_STATE_CONNECTED,
    APP_AV_STATE_DISCONNECTING,
};

/* sub states of APP_AV_STATE_CONNECTED */
enum {
    APP_AV_MEDIA_STATE_IDLE,
    APP_AV_MEDIA_STATE_STARTING,
    APP_AV_MEDIA_STATE_STARTED,
    APP_AV_MEDIA_STATE_STOPPING,
};

/*********************************
 * STATIC FUNCTION DECLARATIONS
 ********************************/

/* handler for bluetooth stack enabled events */
static void __attribute__((unused)) bt_av_hdl_stack_evt(uint16_t event, void *p_param);

/* avrc controller event handler */
static void bt_av_hdl_avrc_ct_evt(uint16_t event, void *p_param);

/* GAP callback function */
static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

/* callback function for A2DP source */
static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);

/* callback function for A2DP source audio data stream */
static int32_t bt_app_a2d_data_cb(uint8_t *data, int32_t len);

/* callback function for AVRCP controller */
static void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);

/* handler for heart beat timer */
static void bt_app_a2d_heart_beat(TimerHandle_t arg);

/* A2DP application state machine */
static void bt_app_av_sm_hdlr(uint16_t event, void *param);

/* utils for transfer BLuetooth Deveice Address into string form */
static char *bda2str(esp_bd_addr_t bda, char *str, size_t size);

/* A2DP application state machine handler for each state */
static void bt_app_av_state_unconnected_hdlr(uint16_t event, void *param);
static void bt_app_av_state_connecting_hdlr(uint16_t event, void *param);
static void bt_app_av_state_connected_hdlr(uint16_t event, void *param);
static void bt_app_av_state_disconnecting_hdlr(uint16_t event, void *param);

/*********************************
 * STATIC VARIABLE DEFINITIONS
 ********************************/

static esp_bd_addr_t s_peer_bda = {0};                        /* Bluetooth Device Address of peer device*/
static uint8_t s_peer_bdname[ESP_BT_GAP_MAX_BDNAME_LEN + 1];  /* Bluetooth Device Name of peer device*/
static int s_a2d_state = APP_AV_STATE_IDLE;                   /* A2DP global state */
static int s_media_state = APP_AV_MEDIA_STATE_IDLE;           /* sub states of APP_AV_STATE_CONNECTED */
static int s_intv_cnt = 0;                                    /* count of heart beat intervals */
static int s_connecting_intv = 0;                             /* count of heart beat intervals for connecting */
static uint32_t s_pkt_cnt = 0;                                /* count of packets */
static esp_avrc_rn_evt_cap_mask_t s_avrc_peer_rn_cap;         /* AVRC target notification event capability bit mask */
static TimerHandle_t s_tmr;                                   /* handle of heart beat timer */
/* suppress unused variable warnings in certain build configs */
static void __attribute__((unused)) _main_suppress_unused(void) { (void)s_tmr; }

static const char remote_device_name[] = CONFIG_EXAMPLE_PEER_DEVICE_NAME;

/* Avoid unused variable warning for remote_device_name in some build configs */
static void __attribute__((unused)) _main_remote_name_used(void) { (void)remote_device_name; }

#if HEAP_MEMORY_DEBUG
static void bt_log_allocator_snapshot(const char *reason, bool dump_entries)
{
    size_t free_default = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest_default = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    uint32_t tracked_current = osi_mem_dbg_get_current_size();
    uint32_t tracked_peak = osi_mem_dbg_get_max_size();
    uint32_t tracked_entries = osi_mem_dbg_get_entry_count();

    ESP_LOGW(BT_AV_TAG,
             "allocator snapshot (%s): tracked_current=%" PRIu32 "B tracked_peak=%" PRIu32
             "B entries=%" PRIu32 " free_default=%zuB free_internal=%zuB largest_default=%zuB",
             reason ? reason : "?",
             tracked_current,
             tracked_peak,
             tracked_entries,
             free_default,
             free_internal,
             largest_default);

    if (dump_entries) {
        osi_mem_dbg_show();
    }
}
#endif

/* keepalive output remains silent to avoid audible beeps */

// Stack size settings for better stability
#define BT_APP_TASK_STACK_SIZE    8192     // Increased from default 4096

/*********************************
 * STATIC FUNCTION DEFINITIONS
 ********************************/

static char *bda2str(esp_bd_addr_t bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }

    safe_snprintf(str, size, "%02x:%02x:%02x:%02x:%02x:%02x",
                  bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    return str;
}

static bool get_name_from_eir(uint8_t *eir, uint8_t *bdname, uint8_t *bdname_len)
{
    uint8_t *rmt_bdname = NULL;
    uint8_t rmt_bdname_len = 0;

    if (!eir) {
        return false;
    }

    /* get complete or short local name from eir data */
    rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname) {
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }

    if (rmt_bdname) {
        if (rmt_bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN) {
            rmt_bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        }

        if (bdname) {
            safe_memcpy(bdname, ESP_BT_GAP_MAX_BDNAME_LEN + 1, rmt_bdname, rmt_bdname_len);
            bdname[rmt_bdname_len] = '\0';
        }
        if (bdname_len) {
            *bdname_len = rmt_bdname_len;
        }
        return true;
    }

    return false;
}

static void filter_inquiry_scan_result(esp_bt_gap_cb_param_t *param)
{
    char bda_str[18];
    uint32_t cod = 0;     /* class of device */
    int32_t rssi = -129;  /* invalid value */
    uint8_t *eir = NULL;
    esp_bt_gap_dev_prop_t *p;

    /* handle the discovery results */
    ESP_LOGI(BT_AV_TAG, "Scanned device: %s", bda2str(param->disc_res.bda, bda_str, 18));
    for (int i = 0; i < param->disc_res.num_prop; i++) {
        p = param->disc_res.prop + i;
        switch (p->type) {
        case ESP_BT_GAP_DEV_PROP_COD:
            cod = *(uint32_t *)(p->val);
            ESP_LOGI(BT_AV_TAG, "--Class of Device: 0x%"PRIx32, cod);
            break;
        case ESP_BT_GAP_DEV_PROP_RSSI:
            rssi = *(int8_t *)(p->val);
            ESP_LOGI(BT_AV_TAG, "--RSSI: %"PRId32, rssi);
            break;
        case ESP_BT_GAP_DEV_PROP_EIR:
            eir = (uint8_t *)(p->val);
            break;
        case ESP_BT_GAP_DEV_PROP_BDNAME:
        default:
            break;
        }
    }

    /* search for device with MAJOR service class as "rendering" in COD */
    if (!esp_bt_gap_is_valid_cod(cod) ||
            !(esp_bt_gap_get_cod_srvc(cod) & ESP_BT_COD_SRVC_RENDERING)) {
        return;
    }

    // Get device name if available in EIR data
    if (eir) {
        get_name_from_eir(eir, s_peer_bdname, NULL);
        
        // Check if device name matches remote_device_name or if we should connect to any audio rendering device
        // Also accept the device if there is a pending manual pairing request for this MAC so a
        // `PAIR <MAC>` command isn't ignored due to the configured target name.
        bool accept_by_name = (strcmp((char *)s_peer_bdname, remote_device_name) == 0) || (strlen(remote_device_name) == 0);
        bool accept_by_pending_pair = false;
        {
            bt_pairing_request_info_t pending = {0};
            if (bt_pairing_get_pending_request(&pending)) {
                if (pending.mac[0] != '\0' && strcmp(pending.mac, bda_str) == 0) {
                    accept_by_pending_pair = true;
                }
            }
        }

        if (accept_by_name || accept_by_pending_pair) {
            ESP_LOGI(BT_AV_TAG, "Found suitable audio device: %s, name: %s", bda_str, s_peer_bdname);
            s_a2d_state = APP_AV_STATE_DISCOVERED;
            safe_memcpy(s_peer_bda, sizeof(s_peer_bda), param->disc_res.bda, ESP_BD_ADDR_LEN);
            ESP_LOGI(BT_AV_TAG, "Cancelling device discovery and connecting to device...");
            esp_bt_gap_cancel_discovery();
        } else {
            ESP_LOGI(BT_AV_TAG, "Device name '%s' doesn't match target '%s', looking for more devices", 
                    s_peer_bdname, remote_device_name);
        }
    }
}

static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    /* when device discovered a result, this event comes */
    case ESP_BT_GAP_DISC_RES_EVT: {
        if (s_a2d_state == APP_AV_STATE_DISCOVERING) {
            filter_inquiry_scan_result(param);
        }
        break;
    }
    /* when discovery state changed, this event comes */
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            if (s_a2d_state == APP_AV_STATE_DISCOVERED) {
                s_a2d_state = APP_AV_STATE_CONNECTING;
                ESP_LOGI(BT_AV_TAG, "Device discovery stopped.");
                ESP_LOGI(BT_AV_TAG, "a2dp connecting to peer: %s", s_peer_bdname);
                /* connect source to peer device specified by Bluetooth Device Address */
                esp_a2d_source_connect(s_peer_bda);
            } else {
                /* not discovered, continue to discover */
                ESP_LOGI(BT_AV_TAG, "Device discovery failed, continue to discover...");
#if HEAP_MEMORY_DEBUG
                bt_log_allocator_snapshot("gap:discovery-stopped-no-peer", true);
#endif
                esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
            }
        } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
            ESP_LOGI(BT_AV_TAG, "Discovery started.");
#if HEAP_MEMORY_DEBUG
            bt_log_allocator_snapshot("gap:discovery-started", false);
#endif
        }
        break;
    }
    /* when authentication completed, this event comes */
    case ESP_BT_GAP_AUTH_CMPL_EVT: {
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(BT_AV_TAG, "authentication success: %s", param->auth_cmpl.device_name);
            ESP_LOG_BUFFER_HEX(BT_AV_TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
        } else {
            ESP_LOGE(BT_AV_TAG, "authentication failed, status: %d", param->auth_cmpl.stat);
        }
        break;
    }
    /* when Legacy Pairing pin code requested, this event comes */
    case ESP_BT_GAP_PIN_REQ_EVT: {
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit: %d", param->pin_req.min_16_digit);
        if (param->pin_req.min_16_digit) {
            ESP_LOGI(BT_AV_TAG, "Input pin code: 0000 0000 0000 0000");
            esp_bt_pin_code_t pin_code = {0};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
        } else {
            ESP_LOGI(BT_AV_TAG, "Input pin code: 1234");
            esp_bt_pin_code_t pin_code;
            pin_code[0] = '1';
            pin_code[1] = '2';
            pin_code[2] = '3';
            pin_code[3] = '4';
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        }
        break;
    }

#if (CONFIG_EXAMPLE_SSP_ENABLED == true)
    /* when Security Simple Pairing user confirmation requested, this event comes */
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %06"PRIu32, param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    /* when Security Simple Pairing passkey notified, this event comes */
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey: %06"PRIu32, param->key_notif.passkey);
        break;
    /* when Security Simple Pairing passkey requested, this event comes */
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
        break;
#endif

    /* when GAP mode changed, this event comes */
    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_MODE_CHG_EVT mode: %d", param->mode_chg.mode);
        break;
    case ESP_BT_GAP_GET_DEV_NAME_CMPL_EVT:
        if (param->get_dev_name_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_GET_DEV_NAME_CMPL_EVT device name: %s", param->get_dev_name_cmpl.name);
        } else {
            ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_GET_DEV_NAME_CMPL_EVT failed, state: %d", param->get_dev_name_cmpl.status);
        }
        break;
    /* other */
    default: {
        ESP_LOGI(BT_AV_TAG, "event: %d", event);
        break;
    }
    }

    return;
}

static void __attribute__((unused)) bt_av_hdl_stack_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_AV_TAG, "%s event: %d", __func__, event);

    switch (event) {
    /* when stack up worked, this event comes */
    case BT_APP_STACK_UP_EVT: {
        char *dev_name = LOCAL_DEVICE_NAME;
        esp_bt_gap_set_device_name(dev_name);
        esp_bt_gap_register_callback(bt_app_gap_cb);

        esp_avrc_ct_init();
        esp_avrc_ct_register_callback(bt_app_rc_ct_cb);

        esp_avrc_rn_evt_cap_mask_t evt_set = {0};
        esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
        ESP_ERROR_CHECK(esp_avrc_tg_set_rn_evt_cap(&evt_set));

        esp_a2d_source_init();
        esp_a2d_register_callback(&bt_app_a2d_cb);
        esp_a2d_source_register_data_callback(bt_app_a2d_data_cb);

        /* Avoid the state error of s_a2d_state caused by the connection initiated by the peer device. */
        esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
        esp_bt_gap_get_device_name();

        ESP_LOGI(BT_AV_TAG, "Starting device discovery...");
        s_a2d_state = APP_AV_STATE_DISCOVERING;
        esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);

        /* create and start heart beat timer */
        do {
            int tmr_id = 0;
            s_tmr = xTimerCreate("connTmr", (10000 / portTICK_PERIOD_MS),
                                 pdTRUE, (void *) &tmr_id, bt_app_a2d_heart_beat);
            xTimerStart(s_tmr, portMAX_DELAY);
        } while (0);
        break;
    }
    /* other */
    default: {
        ESP_LOGE(BT_AV_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
    }
}

static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    bt_app_work_dispatch(bt_app_av_sm_hdlr, event, param, sizeof(esp_a2d_cb_param_t), NULL);
}

/* keepalive callback: fill buffer with silence to avoid audible beeps */
static int32_t bt_app_a2d_data_cb(uint8_t *data, int32_t len)
{
    if (data == NULL || len < 0) {
        return 0;
    }
    safe_memset(data, (size_t)len, 0, (size_t)len);
    return len;
}

static void bt_app_a2d_heart_beat(TimerHandle_t arg)
{
    bt_app_work_dispatch(bt_app_av_sm_hdlr, BT_APP_HEART_BEAT_EVT, NULL, 0, NULL);
}

static void bt_app_av_sm_hdlr(uint16_t event, void *param)
{
    ESP_LOGI(BT_AV_TAG, "%s state: %d, event: 0x%x", __func__, s_a2d_state, event);

    /* select handler according to different states */
    switch (s_a2d_state) {
    case APP_AV_STATE_DISCOVERING:
    case APP_AV_STATE_DISCOVERED:
        break;
    case APP_AV_STATE_UNCONNECTED:
        bt_app_av_state_unconnected_hdlr(event, param);
        break;
    case APP_AV_STATE_CONNECTING:
        bt_app_av_state_connecting_hdlr(event, param);
        break;
    case APP_AV_STATE_CONNECTED:
        bt_app_av_state_connected_hdlr(event, param);
        break;
    case APP_AV_STATE_DISCONNECTING:
        bt_app_av_state_disconnecting_hdlr(event, param);
        break;
    default:
        ESP_LOGE(BT_AV_TAG, "%s invalid state: %d", __func__, s_a2d_state);
        break;
    }
}

static void bt_app_av_state_unconnected_hdlr(uint16_t event, void *param)
{
    esp_a2d_cb_param_t *a2d = NULL;
    /* handle the events of interest in unconnected state */
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
    case ESP_A2D_AUDIO_STATE_EVT:
    case ESP_A2D_AUDIO_CFG_EVT:
    case ESP_A2D_MEDIA_CTRL_ACK_EVT:
        break;
    case BT_APP_HEART_BEAT_EVT: {
        uint8_t *bda = s_peer_bda;
        ESP_LOGI(BT_AV_TAG, "a2dp connecting to peer: %02x:%02x:%02x:%02x:%02x:%02x",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        esp_a2d_source_connect(s_peer_bda);
        s_a2d_state = APP_AV_STATE_CONNECTING;
        s_connecting_intv = 0;
        break;
    }
    case ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT: {
        a2d = (esp_a2d_cb_param_t *)(param);
        ESP_LOGI(BT_AV_TAG, "%s, delay value: %u * 1/10 ms", __func__, a2d->a2d_report_delay_value_stat.delay_value);
        break;
    }
    default: {
        ESP_LOGE(BT_AV_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
    }
}

static void bt_app_av_state_connecting_hdlr(uint16_t event, void *param)
{
    esp_a2d_cb_param_t *a2d = NULL;

    /* handle the events of interest in connecting state */
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
        a2d = (esp_a2d_cb_param_t *)(param);
        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            ESP_LOGI(BT_AV_TAG, "a2dp connected");
            s_a2d_state =  APP_AV_STATE_CONNECTED;
            s_media_state = APP_AV_MEDIA_STATE_IDLE;
        } else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            s_a2d_state =  APP_AV_STATE_UNCONNECTED;
        }
        break;
    }
    case ESP_A2D_AUDIO_STATE_EVT:
    case ESP_A2D_AUDIO_CFG_EVT:
    case ESP_A2D_MEDIA_CTRL_ACK_EVT:
        break;
    case BT_APP_HEART_BEAT_EVT:
        /**
         * Switch state to APP_AV_STATE_UNCONNECTED
         * when connecting lasts more than 2 heart beat intervals.
         */
        if (++s_connecting_intv >= 2) {
            s_a2d_state = APP_AV_STATE_UNCONNECTED;
            s_connecting_intv = 0;
        }
        break;
    case ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT: {
        a2d = (esp_a2d_cb_param_t *)(param);
        ESP_LOGI(BT_AV_TAG, "%s, delay value: %u * 1/10 ms", __func__, a2d->a2d_report_delay_value_stat.delay_value);
        break;
    }
    default:
        ESP_LOGE(BT_AV_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
}

static void bt_app_av_media_proc(uint16_t event, void *param)
{
    esp_a2d_cb_param_t *a2d = NULL;

    switch (s_media_state) {
    case APP_AV_MEDIA_STATE_IDLE: {
        if (event == BT_APP_HEART_BEAT_EVT) {
            ESP_LOGI(BT_AV_TAG, "a2dp media ready checking ...");
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
        } else if (event == ESP_A2D_MEDIA_CTRL_ACK_EVT) {
            a2d = (esp_a2d_cb_param_t *)(param);
            if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY &&
                    a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                ESP_LOGI(BT_AV_TAG, "a2dp media ready, starting ...");
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
                s_media_state = APP_AV_MEDIA_STATE_STARTING;
            }
        }
        break;
    }
    case APP_AV_MEDIA_STATE_STARTING: {
        if (event == ESP_A2D_MEDIA_CTRL_ACK_EVT) {
            a2d = (esp_a2d_cb_param_t *)(param);
            if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_START &&
                    a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                ESP_LOGI(BT_AV_TAG, "a2dp media start successfully.");
                s_intv_cnt = 0;
                s_media_state = APP_AV_MEDIA_STATE_STARTED;
            } else {
                /* not started successfully, transfer to idle state */
                ESP_LOGI(BT_AV_TAG, "a2dp media start failed.");
                s_media_state = APP_AV_MEDIA_STATE_IDLE;
            }
        }
        break;
    }
    case APP_AV_MEDIA_STATE_STARTED: {
        if (event == BT_APP_HEART_BEAT_EVT) {
            /* stop media after 10 heart beat intervals */
            if (++s_intv_cnt >= 10) {
                ESP_LOGI(BT_AV_TAG, "a2dp media suspending...");
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
                s_media_state = APP_AV_MEDIA_STATE_STOPPING;
                s_intv_cnt = 0;
            }
        }
        break;
    }
    case APP_AV_MEDIA_STATE_STOPPING: {
        if (event == ESP_A2D_MEDIA_CTRL_ACK_EVT) {
            a2d = (esp_a2d_cb_param_t *)(param);
            if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_SUSPEND &&
                    a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                ESP_LOGI(BT_AV_TAG, "a2dp media suspend successfully, disconnecting...");
                s_media_state = APP_AV_MEDIA_STATE_IDLE;
                esp_a2d_source_disconnect(s_peer_bda);
                s_a2d_state = APP_AV_STATE_DISCONNECTING;
            } else {
                ESP_LOGI(BT_AV_TAG, "a2dp media suspending...");
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
            }
        }
        break;
    }
    default: {
        break;
    }
    }
}

static void bt_app_av_state_connected_hdlr(uint16_t event, void *param)
{
    esp_a2d_cb_param_t *a2d = NULL;

    /* handle the events of interest in connected state */
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
        a2d = (esp_a2d_cb_param_t *)(param);
        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            ESP_LOGI(BT_AV_TAG, "a2dp disconnected");
            s_a2d_state = APP_AV_STATE_UNCONNECTED;
        }
        break;
    }
    case ESP_A2D_AUDIO_STATE_EVT: {
        a2d = (esp_a2d_cb_param_t *)(param);
        if (ESP_A2D_AUDIO_STATE_STARTED == a2d->audio_stat.state) {
            s_pkt_cnt = 0;
        }
        break;
    }
    case ESP_A2D_AUDIO_CFG_EVT:
        // not supposed to occur for A2DP source
        break;
    case ESP_A2D_MEDIA_CTRL_ACK_EVT:
    case BT_APP_HEART_BEAT_EVT: {
        bt_app_av_media_proc(event, param);
        break;
    }
    case ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT: {
        a2d = (esp_a2d_cb_param_t *)(param);
        ESP_LOGI(BT_AV_TAG, "%s, delay value: %u * 1/10 ms", __func__, a2d->a2d_report_delay_value_stat.delay_value);
        break;
    }
    default: {
        ESP_LOGE(BT_AV_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
    }
}

static void bt_app_av_state_disconnecting_hdlr(uint16_t event, void *param)
{
    esp_a2d_cb_param_t *a2d = NULL;

    /* handle the events of interest in disconnecing state */
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
        a2d = (esp_a2d_cb_param_t *)(param);
        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            ESP_LOGI(BT_AV_TAG, "a2dp disconnected");
            s_a2d_state =  APP_AV_STATE_UNCONNECTED;
        }
        break;
    }
    case ESP_A2D_AUDIO_STATE_EVT:
    case ESP_A2D_AUDIO_CFG_EVT:
    case ESP_A2D_MEDIA_CTRL_ACK_EVT:
    case BT_APP_HEART_BEAT_EVT:
        break;
    case ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT: {
        a2d = (esp_a2d_cb_param_t *)(param);
        ESP_LOGI(BT_AV_TAG, "%s, delay value: 0x%u * 1/10 ms", __func__, a2d->a2d_report_delay_value_stat.delay_value);
        break;
    }
    default: {
        ESP_LOGE(BT_AV_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
    }
}

/* callback function for AVRCP controller */
static void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
    case ESP_AVRC_CT_METADATA_RSP_EVT:
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:
    case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT: {
        bt_app_work_dispatch(bt_av_hdl_avrc_ct_evt, event, param, sizeof(esp_avrc_ct_cb_param_t), NULL);
        break;
    }
    default: {
        ESP_LOGE(BT_RC_CT_TAG, "Invalid AVRC event: %d", event);
        break;
    }
    }
}

static void bt_av_volume_changed(void)
{
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,
                                           ESP_AVRC_RN_VOLUME_CHANGE)) {
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_VOLUME_CHANGE, ESP_AVRC_RN_VOLUME_CHANGE, 0);
    }
}

void bt_av_notify_evt_handler(uint8_t event_id, esp_avrc_rn_param_t *event_parameter)
{
    switch (event_id) {
    /* when volume changed locally on target, this event comes */
    case ESP_AVRC_RN_VOLUME_CHANGE: {
        ESP_LOGI(BT_RC_CT_TAG, "Volume changed: %d", event_parameter->volume);
        ESP_LOGI(BT_RC_CT_TAG, "Set absolute volume: volume %d", event_parameter->volume + 5);
        esp_avrc_ct_send_set_absolute_volume_cmd(APP_RC_CT_TL_RN_VOLUME_CHANGE, event_parameter->volume + 5);
        bt_av_volume_changed();
        break;
    }
    /* other */
    default:
        break;
    }
}

/* AVRC controller event handler */
static void __attribute__((unused)) bt_av_hdl_avrc_ct_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_RC_CT_TAG, "%s evt %d", __func__, event);
    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *)(p_param);

    switch (event) {
    /* when connection state changed, this event comes */
    case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
        uint8_t *bda = rc->conn_stat.remote_bda;
        ESP_LOGI(BT_RC_CT_TAG, "AVRC conn_state event: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                 rc->conn_stat.connected, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

        if (rc->conn_stat.connected) {
            esp_avrc_ct_send_get_rn_capabilities_cmd(APP_RC_CT_TL_GET_CAPS);
        } else {
            s_avrc_peer_rn_cap.bits = 0;
        }
        break;
    }
    /* when passthrough responded, this event comes */
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT: {
        ESP_LOGI(BT_RC_CT_TAG, "AVRC passthrough response: key_code 0x%x, key_state %d, rsp_code %d", rc->psth_rsp.key_code,
                    rc->psth_rsp.key_state, rc->psth_rsp.rsp_code);
        break;
    }
    /* when metadata responded, this event comes */
    case ESP_AVRC_CT_METADATA_RSP_EVT: {
        ESP_LOGI(BT_RC_CT_TAG, "AVRC metadata response: attribute id 0x%x, %s", rc->meta_rsp.attr_id, rc->meta_rsp.attr_text);
        free(rc->meta_rsp.attr_text);
        break;
    }
    /* when notification changed, this event comes */
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT: {
        ESP_LOGI(BT_RC_CT_TAG, "AVRC event notification: %d", rc->change_ntf.event_id);
        bt_av_notify_evt_handler(rc->change_ntf.event_id, &rc->change_ntf.event_parameter);
        break;
    }
    /* when indicate feature of remote device, this event comes */
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT: {
        ESP_LOGI(BT_RC_CT_TAG, "AVRC remote features %"PRIx32", TG features %x", rc->rmt_feats.feat_mask, rc->rmt_feats.tg_feat_flag);
        break;
    }
    /* when get supported notification events capability of peer device, this event comes */
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
        ESP_LOGI(BT_RC_CT_TAG, "remote rn_cap: count %d, bitmask 0x%x", rc->get_rn_caps_rsp.cap_count,
                 rc->get_rn_caps_rsp.evt_set.bits);
        s_avrc_peer_rn_cap.bits = rc->get_rn_caps_rsp.evt_set.bits;

        bt_av_volume_changed();
        break;
    }
    /* when set absolute volume responded, this event comes */
    case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT: {
        ESP_LOGI(BT_RC_CT_TAG, "Set absolute volume response: volume %d", rc->set_volume_rsp.volume);
        break;
    }
    /* other */
    default: {
        ESP_LOGE(BT_RC_CT_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
    }
}

/* The legacy static `bt_init()` implementation was removed in favor of the
 * centralized `bt_manager` startup sequence. Initialization is now handled
 * by `bt_manager_init()` called from `app_main()` so the legacy, file-scoped
 * helper is no longer necessary.
 */

/*********************************
 * MAIN ENTRY POINT
 ********************************/

void app_main(void)
{
    // Free BLE controller memory to give Classic BT more DRAM
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));    

    /* Very early unbuffered boot marker for programmatic captures. This
     * appears before most initialization and helps external injectors
     * avoid racing the device boot sequence. Use both printf and the
     * ROM-level esp_rom_printf when available. */
    printf("DIAG|BOOT|EARLY_BOOT_MARKER\r\n");
#ifdef esp_rom_printf
    esp_rom_printf("DIAG|BOOT|EARLY_BOOT_MARKER\r\n");
#endif

    ESP_LOGI(BT_AV_TAG, "ESP32 Bluetooth Audio Source starting");
    /* Quiet the very chatty audio processor logs so CLI commands aren't
     * drowned out during diagnostics. Raise as needed when deep-diving.
     */
    esp_log_level_set("AUDIO_PROC", ESP_LOG_WARN);
    /* Ensure the console UART driver is installed early so command layer
     * can synchronously read/write without racing with other subsystems.
     * This mirrors the conservative install performed in the command
     * interface but moves it earlier in startup to reduce races. */
#ifdef ESP_PLATFORM
    {
    const int uart_rx_buf = 1024;
    const int uart_tx_buf = 1024;
    /* Resolve the console UART number from config when available; fall
     * back to UART_NUM_0 which is commonly the primary UART on ESP32.
     */
#ifdef CONFIG_ESP_CONSOLE_UART_NUM
    int console_uart = CONFIG_ESP_CONSOLE_UART_NUM;
#else
    int console_uart = UART_NUM_0;
#endif
    /* Best-effort delete then install to ensure a clean state */
    (void)uart_driver_delete(console_uart);
    esp_err_t r = uart_driver_install(console_uart, uart_rx_buf, uart_tx_buf, 0, NULL, 0);
    printf("DIAG|BOOT|EARLY_UART_INSTALL|ret=%d,installed=%d\r\n", (int)r, uart_is_driver_installed(console_uart) ? 1 : 0);
#ifdef esp_rom_printf
    esp_rom_printf("DIAG|BOOT|EARLY_UART_INSTALL|ret=%d,installed=%d\r\n", (int)r, uart_is_driver_installed(console_uart) ? 1 : 0);
#endif
    if (uart_is_driver_installed(console_uart)) {
        const char ready[] = "DIAG|BOOT|UART_READY_FOR_CMD_LAYER\r\n";
        uart_write_bytes(console_uart, ready, sizeof(ready)-1);
        /* Ensure the ready string is transmitted before proceeding so host
         * injectors that watch for this marker don't miss it due to buffering. */
        (void)uart_wait_tx_done(console_uart, pdMS_TO_TICKS(50));
    }
    }
#endif
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize and start Bluetooth via bt_manager so the command interface
    // and other components using the manager APIs are ready for SCAN/PAIR.
    bt_manager_init_t bt_cfg = {
        .device_name = LOCAL_DEVICE_NAME,
        .connected_cb = NULL,
        .disconnected_cb = NULL,
    };

    if (bt_manager_init(&bt_cfg) != ESP_OK) {
        ESP_LOGE(BT_AV_TAG, "bt_manager_init failed");
    } else {
        ESP_LOGI(BT_AV_TAG, "Bluetooth manager initialized");
    }

    // Initialize command interface for serial commands and events
    // This will create the UART driver and prepare the command parser.
    // The cmd_process() function must be called regularly; create a
    // small FreeRTOS task to poll for incoming command lines.
#ifdef ESP_PLATFORM
    if (cmd_init() != CMD_SUCCESS) {
        ESP_LOGW(BT_AV_TAG, "cmd_init() failed or already initialized");
    }
    /* Unconditional boot-time diagnostic: print a plain-text marker so
     * non-interactive captures can reliably detect that command
     * initialization has completed and which UART is in use. Use printf
     * to ensure the message appears on the console even if the UART driver
     * isn't fully installed yet. */
    printf("INFO|CMD_IF|BOOT_DIAG|CMD_INIT_CALLED\r\n");
    /* Also emit an unbuffered ROM-level print so host captures see this even if stdio is buffered */
#ifdef CONFIG_IDF_TARGET_ESP32
    esp_rom_printf("INFO|CMD_IF|BOOT_DIAG|CMD_INIT_CALLED\r\n");
#endif

    // Create a tiny task dedicated to processing command input
    xTaskCreate(cmd_process_task, "cmd_proc", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
    /* One-time diagnostic to indicate the command processing task was created
     * and should be running. This helps programmatic captures detect that the
     * task is active even when the UART driver installation may be delayed. */
    printf("INFO|CMD_IF|CMD_TASK_STARTED\r\n");
    /* duplicate as ROM-level immediate output to reduce race with host capture */
#ifdef CONFIG_IDF_TARGET_ESP32
    esp_rom_printf("INFO|CMD_IF|CMD_TASK_STARTED\r\n");
#endif
#ifdef esp_rom_printf
    esp_rom_printf("INFO|CMD_IF|CMD_TASK_STARTED\r\n");
#endif

    /* Runtime diagnostic: print whether the UART driver is installed for the command UART */
    int uart_installed = uart_is_driver_installed(CONFIG_ESP_CONSOLE_UART_NUM);
    printf("DIAG|CMD_IF|UART_DRIVER_INSTALLED|uart=%d|installed=%d\r\n", CONFIG_ESP_CONSOLE_UART_NUM, uart_installed);
#ifdef ESP_PLATFORM
    /* Auto-initialize and start audio/I2S at boot.
     * Behavior: attempt to read persisted I2S pin overrides from NVS and
     * fall back to the component defaults. On success the audio pipeline
     * will be initialized and the I2S RX channel enabled so downstream
     * consumers (A2DP source) can immediately stream live audio.
     *
     * Note: this intentionally changes the previous deferred-init behavior
     * to enable I2S at boot. If you prefer deferred init revert this block
     * to the prior diagnostic-only query.
     */
    {
        audio_config_t aconf = {
            .sample_rate = AUDIO_SAMPLE_RATE_44K,
            .bit_depth = AUDIO_BIT_DEPTH_16,
            .channels = AUDIO_CHANNEL_STEREO,
            .volume = 80,
            .mute = false,
            .i2s_port = I2S_NUM_0,
            /* fallbacks chosen to match internal defaults in audio_processor.c */
            .i2s_bclk_pin = GPIO_NUM_26,
            .i2s_ws_pin = GPIO_NUM_25,
            .i2s_din_pin = GPIO_NUM_22,
            .i2s_dout_pin = GPIO_NUM_NC,
        };

        int bclk = -1, ws = -1, din = -1, dout = -1;
        /* Best-effort: if NVS has stored pins use them */
        if (nvs_storage_get_i2s_pins(&bclk, &ws, &din, &dout) == ESP_OK) {
            if (bclk >= 0) aconf.i2s_bclk_pin = bclk;
            if (ws >= 0) aconf.i2s_ws_pin = ws;
            if (din >= 0) aconf.i2s_din_pin = din;
            if (dout >= 0) aconf.i2s_dout_pin = dout;
        }

        esp_err_t aerr = audio_processor_init(&aconf);
        if (aerr != ESP_OK) {
            printf("DIAG|AUDIO|STATUS|init_failed|err=%s\r\n", esp_err_to_name(aerr));
        } else {
            aerr = audio_processor_start();
            if (aerr != ESP_OK) {
                printf("DIAG|AUDIO|STATUS|start_failed|err=%s\r\n", esp_err_to_name(aerr));
            } else {
                printf("DIAG|AUDIO|STATUS|initialized=1|running=1|volume=%u|mute=%d|rate=%d|bits=%d|ch=%d\r\n",
                       (unsigned)aconf.volume,
                       aconf.mute ? 1 : 0,
                       (int)aconf.sample_rate,
                       (int)aconf.bit_depth,
                       (int)aconf.channels);
            }
        }
    }
#endif
#endif
    
    ESP_LOGI(BT_AV_TAG, "====================================================");
    ESP_LOGI(BT_AV_TAG, "Bluetooth will scan for and connect to audio devices");
    ESP_LOGI(BT_AV_TAG, "Will play a short beep once per minute");
    ESP_LOGI(BT_AV_TAG, "====================================================");
    
    // Main loop - not needed as FreeRTOS tasks are now running
    while (1) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);  // Just a background task
    }
}
