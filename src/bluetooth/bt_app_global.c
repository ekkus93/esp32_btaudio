#include "bluetooth/bt_app_global.h"

int num_discovered_devices = 0;

bool pin_required = false;
esp_bd_addr_t pending_pair_addr = {0};

int s_a2d_state = APP_AV_STATE_IDLE;
int s_media_state = APP_AV_MEDIA_STATE_IDLE;

bool s_beep_in_progress = false;
int s_beep_duration = 0;

bool sine_table_initialized = false;
int s_beep_index = 0;

uint8_t s_current_volume = 0;

int s_congestion_count = 0;
bool s_severe_congestion = false;
uint32_t s_last_congestion_time = 0;

bool s_l2cap_congestion_flag = false;
uint32_t s_last_operation_time = 0;

bool s_volume_initialized = false;

SemaphoreHandle_t s_bt_resource_mutex = NULL;