#ifndef RADIO_H
#define RADIO_H

#include "esp_err.h"
#include <stdbool.h>

// Function declarations
esp_err_t radio_play(const char *url);
esp_err_t radio_stop(void);
esp_err_t radio_play_stream(const char *url);
void radio_set_active(bool active);

typedef struct {
    FILE *fp;
    size_t size;
    const char *url;  // Add this member for URL-based streaming
} radio_task_params_t;

void radio_task(void *param); // Add this line

#endif // RADIO_H
