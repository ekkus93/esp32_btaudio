#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Discovery state enum */
typedef enum {
    ESP_BT_GAP_DISCOVERY_STOPPED = 0,
    ESP_BT_GAP_DISCOVERY_STARTED,
} esp_bt_gap_discovery_state_t;

/* Inquiry mode enum */
typedef enum {
    ESP_BT_INQ_MODE_GENERAL_INQUIRY = 0,
    ESP_BT_INQ_MODE_LIMITED_INQUIRY,
} esp_bt_inq_mode_t;

/* Device property type enum */
typedef enum {
    ESP_BT_GAP_DEV_PROP_BDNAME = 0,
    ESP_BT_GAP_DEV_PROP_COD,
    ESP_BT_GAP_DEV_PROP_RSSI,
    ESP_BT_GAP_DEV_PROP_EIR,
} esp_bt_gap_dev_prop_type_t;

/* Device property structure */
typedef struct {
    esp_bt_gap_dev_prop_type_t type;
    void *val;
    int len;
} esp_bt_gap_dev_prop_t;

typedef struct {
    uint8_t dummy;
} esp_bt_gap_cb_param_t;

typedef int esp_bt_gap_cb_event_t;

typedef void (*esp_bt_gap_cb_t)(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

/* GAP discovery function declarations */
esp_err_t esp_bt_gap_start_discovery(esp_bt_inq_mode_t mode, uint8_t inq_len, uint8_t num_rsps);
esp_err_t esp_bt_gap_cancel_discovery(void);

#ifdef __cplusplus
}
#endif
