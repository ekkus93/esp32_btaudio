#pragma once

#include <stdbool.h>
#include "esp_bt.h"

#ifdef __cplusplus
extern "C" {
#endif

void mock_a2dp_reset(void);
void mock_a2dp_set_connect_result(esp_bt_status_t result);
void mock_a2dp_set_disconnect_result(esp_bt_status_t result);
void mock_a2dp_set_media_ctrl_result(esp_bt_status_t result);
void mock_a2dp_set_init_result(esp_bt_status_t result);
void mock_a2dp_set_deinit_result(esp_bt_status_t result);
int mock_a2dp_get_connect_calls(void);
int mock_a2dp_get_disconnect_calls(void);
const char *mock_a2dp_get_last_connect_addr(void);
const char *mock_a2dp_get_last_disconnect_addr(void);
bool mock_a2dp_was_init_called(void);
bool mock_a2dp_was_deinit_called(void);
esp_a2d_cb_t mock_a2dp_get_registered_callback(void);
esp_a2d_source_data_cb_t mock_a2dp_get_registered_data_callback(void);

#ifdef __cplusplus
}
#endif
