#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t dummy;
} esp_bt_gap_cb_param_t;

typedef int esp_bt_gap_cb_event_t;

typedef void (*esp_bt_gap_cb_t)(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

#ifdef __cplusplus
}
#endif
