#ifndef RADIO_H
#define RADIO_H

#include "esp_err.h"

// Define the radio task parameters structure
typedef struct {
    FILE *fp;
    size_t size;
} radio_task_params_t;

// Function prototypes
void radio_task(void *param);
esp_err_t radio_play(const char *url);
esp_err_t radio_stop(void);
void radio_set_active(bool active);
esp_err_t radio_play_stream(const char *url);

// NEW: Add prototype for accessing the radio task finished flag
bool radio_task_is_finished(void);

#endif // RADIO_H