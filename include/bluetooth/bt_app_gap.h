#ifndef BT_APP_GAP_H
#define BT_APP_GAP_H

#include <esp_gap_bt_api.h>

void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

#endif // BT_APP_GAP_H