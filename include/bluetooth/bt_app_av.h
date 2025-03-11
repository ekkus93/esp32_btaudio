#ifndef BT_APP_AV_H
#define BT_APP_AV_H

#include "esp_err.h"

#define VOLUME_STEP 5  // Define the volume step

void bt_av_hdl_stack_evt(uint16_t event, void *p_param);

esp_err_t init_avrc(void);
esp_err_t init_a2dp(void);
esp_err_t bluetooth_volume_up(void);
esp_err_t bluetooth_volume_down(void);
esp_err_t bluetooth_set_volume(uint8_t volume);
uint8_t bluetooth_get_current_volume(void); // Ensure this matches bt_app_audio.h

#endif // BT_APP_AV_H
