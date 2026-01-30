#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	ESP_AVRC_CT_CONNECTION_STATE_EVT = 0,
	ESP_AVRC_CT_PASSTHROUGH_RSP_EVT,
	ESP_AVRC_CT_REMOTE_FEATURES_EVT,
} esp_avrc_ct_cb_event_t;

typedef enum {
	ESP_AVRC_PT_CMD_PLAY = 0x44,
	ESP_AVRC_PT_CMD_PAUSE = 0x46,
} esp_avrc_pt_cmd_t;

typedef enum {
	ESP_AVRC_PT_CMD_STATE_PRESSED = 0,
	ESP_AVRC_PT_CMD_STATE_RELEASED,
} esp_avrc_pt_cmd_state_t;

typedef struct {
	bool connected;
	uint8_t remote_bda[6];
} esp_avrc_conn_stat_t;

typedef struct {
	esp_avrc_pt_cmd_t key_code;
	esp_avrc_pt_cmd_state_t key_state;
} esp_avrc_psth_rsp_t;

typedef struct {
	uint32_t feat_mask;
} esp_avrc_rmt_feats_t;

typedef union esp_avrc_ct_cb_param_t {
	esp_avrc_conn_stat_t conn_stat;
	esp_avrc_psth_rsp_t psth_rsp;
	esp_avrc_rmt_feats_t rmt_feats;
} esp_avrc_ct_cb_param_t;

typedef void (*esp_avrc_ct_cb_t)(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);

// AVRCP controller APIs (mocked for host tests)
esp_err_t esp_avrc_ct_init(void);
esp_err_t esp_avrc_ct_register_callback(esp_avrc_ct_cb_t callback);
esp_err_t esp_avrc_ct_deinit(void);

#ifdef __cplusplus
}
#endif
