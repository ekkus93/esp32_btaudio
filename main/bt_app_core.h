#ifndef __BT_APP_CORE_H__
#define __BT_APP_CORE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"

#define BT_APP_TAG             "BT_APP"

// Update scan parameters
#define BT_SCAN_DURATION       60    // Increase duration to 60 seconds
#define BT_SCAN_MODE          ESP_BT_INQUIRY_MODE_GENERAL_INQUIRY
#define BT_SCAN_MAX_RESULTS   20

#define MAX_BT_DEVICES         20    // Maximum number of devices to remember

// Add structure for device tracking
typedef struct {
    esp_bd_addr_t bda;
    char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
    esp_bt_gap_dev_prop_t *prop;
    int num_prop;
} bt_device_t;

// Function declarations
void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
void bt_app_start_discovery(void);
esp_err_t bt_app_init(void);

#endif /* __BT_APP_CORE_H__ */
