#ifndef BT_APP_A2DP_H
#define BT_APP_A2DP_H

#include "esp_avrc_api.h"
#include "esp_a2dp_api.h"
#include "esp_err.h"

void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);
int32_t a2dp_source_data_cb(uint8_t *data, int32_t len);
esp_err_t init_a2dp(void);

// AVRCP controller callback
void avrc_ct_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);

#endif // BT_APP_A2DP_H