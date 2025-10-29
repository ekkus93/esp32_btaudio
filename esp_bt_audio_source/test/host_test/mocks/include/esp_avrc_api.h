#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef int esp_avrc_ct_cb_event_t;
typedef struct esp_avrc_ct_cb_param_t esp_avrc_ct_cb_param_t;
typedef void (*esp_avrc_ct_cb_t)(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);

#ifdef __cplusplus
}
#endif
