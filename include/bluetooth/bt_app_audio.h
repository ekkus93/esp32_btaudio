#ifndef BT_APP_AUDIO_H
#define BT_APP_AUDIO_H

#include <stdbool.h> 
#include <stdint.h>
#include "esp_err.h"

// Audio buffer management functions
void bt_app_audio_init(void);
uint8_t* bt_app_audio_get_buffer(void);
void bt_app_audio_release_buffer(uint8_t* buffer);
int bt_app_audio_available_buffers(void);

void trigger_beep(void);
esp_err_t bluetooth_write_audio(const uint8_t* data, size_t* written);
bool is_operation_time_ok(void);
esp_err_t bluetooth_send_beep(void); 

#endif // BT_APP_AUDIO_H