#include "bluetooth/bt_app_global.h"
#include "bluetooth/bt_app_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_bt_defs.h"

// Discovery variables
int num_discovered_devices = 0;  // Tracks number of discovered BT devices

// Pairing variables
bool pin_required = false;       // Whether PIN is required for pairing
esp_bd_addr_t pending_pair_addr = {0};  // Address of device pending pairing

// Connection state variables
int s_a2d_state = 0;    // Current A2DP connection state
int s_media_state = 0;  // Current media streaming state

// Audio control variables
uint8_t s_current_volume = 0;  // Current volume level (0-127)
bool s_volume_initialized = false;          // Whether volume was initialized this boot

// Congestion control variables
int s_congestion_count = 0;        // Count of congestion events
bool s_severe_congestion = false;  // Whether severe congestion is occurring
uint32_t s_last_congestion_time = 0;  // Timestamp of last congestion event

// L2CAP and operation timing variables
bool s_l2cap_congestion_flag = false;  // Whether L2CAP layer is congested
uint32_t s_last_operation_time = 0;    // Timestamp of last BT operation

// Synchronization primitives
SemaphoreHandle_t s_bt_resource_mutex = NULL;  // Define the mutex

// Add these variable definitions
TaskHandle_t s_waiting_task = NULL;
bool s_operation_complete = false;
int s_pairing_attempt = 0;
esp_bd_addr_t s_last_pairing_attempt = {0};
bool s_pairing_in_progress = false;