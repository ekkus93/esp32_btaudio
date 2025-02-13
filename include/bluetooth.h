#ifndef __BLUETOOTH_H__
#define __BLUETOOTH_H__

#include "esp_err.h"
#include "esp_gap_bt_api.h"
#include <stdint.h>
#include <stddef.h>
#include "esp_avrc_api.h" // Add this include

// Function declarations
esp_err_t bluetooth_init(void);
esp_err_t bluetooth_start_discovery(void);
esp_err_t bluetooth_pair_device(const char *mac_str, bool require_pin);
esp_err_t bluetooth_connect_device(const char *mac_str);
esp_err_t bluetooth_disconnect_device(void);
esp_err_t bluetooth_unpair_device(void);
esp_err_t bluetooth_set_device_name(const char *name);
esp_err_t bluetooth_get_device_name(char *name, size_t max_len);
esp_err_t bluetooth_send_beep(void);
esp_err_t init_a2dp(void);
esp_err_t bluetooth_write_audio(const uint8_t* data, size_t* written); // Update this line to accept const pointer

// Event handler declarations
void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
void avrc_ct_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);
void bt_av_hdl_stack_evt(uint16_t event, void *p_param);

#endif /* __BLUETOOTH_H__ */
