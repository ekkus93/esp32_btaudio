#ifndef __BLUETOOTH_H__
#define __BLUETOOTH_H__

#include "esp_err.h"
#include "esp_gap_bt_api.h"

// Function declarations
esp_err_t bluetooth_init(void);
esp_err_t bluetooth_start_discovery(void);
esp_err_t bluetooth_pair_device(const char *mac_str, bool require_pin);
void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

#endif /* __BLUETOOTH_H__ */
