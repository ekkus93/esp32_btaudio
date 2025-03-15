#ifndef RADIO_H
#define RADIO_H

#include "esp_err.h"
#include <stdbool.h>

// Function declarations
void play_mp3_task(void* url);
esp_err_t mp3_play_file(const char* url);
esp_err_t mp3_play_stream(const char* url);
esp_err_t radio_stop(void);
void radio_set_active(bool active);
bool radio_task_is_finished(void);

#endif // RADIO_H
