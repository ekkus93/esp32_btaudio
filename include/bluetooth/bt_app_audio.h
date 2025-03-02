#ifndef BT_APP_AUDIO_H
#define BT_APP_AUDIO_H

#include <stdbool.h> 
#include "esp_err.h"

void trigger_beep(void);
esp_err_t bluetooth_write_audio(const uint8_t* data, size_t* written);
bool is_operation_time_ok(void);
esp_err_t bluetooth_send_beep(void); 

#endif // BT_APP_AUDIO_H