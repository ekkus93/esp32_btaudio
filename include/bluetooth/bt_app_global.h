#ifndef BT_APP_GLOBAL_H
#define BT_APP_GLOBAL_H

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>  // ...existing includes...
#include "esp_bt_defs.h"
#include <math.h>  // Add this for sinf()
#include <inttypes.h>  // Add this for PRId32
#include "esp_bt_device.h" // Add this line
#include "custom_log.h"
#include "esp_avrc_api.h"  // Include AVRCP API
#include "driver/uart.h"
#include "esp_timer.h"  // For esp_timer_get_time
#include "bluetooth/bt_app_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Remove the redundant definition of BT_APP_STACK_UP_EVT
// #define BT_APP_STACK_UP_EVT 0x0000    // << New definition

extern SemaphoreHandle_t s_bt_resource_mutex;  // Declare as extern

// Reduce L2CAP buffer size (adjust as needed)
#define L2CAP_MTU 512  // Reduced from default
#define L2CAP_TX_BUF_SIZE 1024 // Reduced from default

#define MAX_DEVICES 50
#define BT_DEVICE_NAME_KEY "bt_name"
#define DEFAULT_BT_DEVICE_NAME "monkfish"

// Define test tone parameters
#define SAMPLE_RATE     44100
#define TONE_FREQUENCY  440  // 440 Hz (A4 note)
#undef TABLE_SIZE
#define TABLE_SIZE 168  // Precomputed sine table size
#define BEEP_DURATION_THRESHOLD (SAMPLE_RATE / 2)  // New: beep lasts about 0.5 seconds

// Add these additional constants
#define BT_VOLUME_KEY "bt_vol"
#define DEFAULT_VOLUME 32

typedef struct {
    uint8_t bda[ESP_BD_ADDR_LEN];
    char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
} discovered_device_t;

extern int num_discovered_devices;

extern bool pin_required;
extern esp_bd_addr_t pending_pair_addr;

extern int s_a2d_state;
extern int s_media_state;

// Replace existing beep state variables:
extern bool s_beep_in_progress;
extern int s_beep_duration;

// New global variables for sine lookup
extern bool sine_table_initialized;
extern int s_beep_index;

// Add this with the other global variables at the top of the file (after the defines)
extern uint8_t s_current_volume;

// Add these new congestion control variables after the existing global variables
#define MAX_CONGESTION_COUNT 5
extern int s_congestion_count;
extern bool s_severe_congestion;
extern uint32_t s_last_congestion_time;
#define CONGESTION_RECOVERY_TIME_MS 2000 // Time to wait after severe congestion

// In the A2DP callback, offload beep processing to core 1 on connection
extern bool s_l2cap_congestion_flag;
extern uint32_t s_last_operation_time;
#define BT_OPERATION_DELAY_MS 300  // At least 300ms between BT operations

// Replace the bt_app_a2d_cb function with a fixed version
// Add flag to track if the volume was initialized this boot cycle
extern bool s_volume_initialized;

extern SemaphoreHandle_t s_bt_resource_mutex;

// Create a new timer task to periodically check for memory issues
#define MEMORY_CHECK_INTERVAL_MS 5000

// Mutex helper functions
bool take_bt_resource_mutex(TickType_t timeout);
void give_bt_resource_mutex(void);

// Add these variable declarations
extern TaskHandle_t s_waiting_task;
extern bool s_operation_complete;

// Add these after the other extern declarations
#define MAX_PAIRING_ATTEMPTS 3
extern int s_pairing_attempt;
extern esp_bd_addr_t s_last_pairing_attempt;
extern bool s_pairing_in_progress;

// Make sure this function is declared here
esp_err_t bluetooth_pair_device(const char *mac_str, bool require_pin);

// Add sine_table declaration
extern int16_t sine_table[TABLE_SIZE];

// Add these declarations
// String descriptions for A2DP connection states
extern const char *s_a2d_conn_state_str[];

// String descriptions for A2DP audio states
extern const char *s_a2d_audio_state_str[];

// Additional state variable
extern esp_a2d_audio_state_t s_audio_state;

// The app_av_media_state_t type is already defined in bt_app_types.h
// which is included at the top of this file

#endif // BT_APP_GLOBAL_H
