#include <string.h>
#include <time.h>
#include <inttypes.h> // Add for PRIu32 macros
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "bt_app_core.h"
#include "bt_source.h"
#include "esp_rom_sys.h"
#include "util_safe.h"
#include "platform_timing.h"
#include "nvs_storage.h"

static const char *TAG = "BT_CONNECTION_MGR";

#define safe_memset util_safe_memset
#define safe_memcpy util_safe_memcpy

/* Static variables for connection management */
static bt_connection_state_t s_connection_state = BT_CONNECTION_STATE_DISCONNECTED;
static bt_streaming_state_t s_streaming_state = BT_STREAMING_STATE_STOPPED;
static bt_connection_info_t s_connection_info = {0};
static bt_streaming_info_t s_streaming_info = {0};
static esp_a2d_connection_state_t s_a2d_conn_state = ESP_A2D_CONNECTION_STATE_DISCONNECTED;
static esp_a2d_audio_state_t s_a2d_audio_state = ESP_A2D_AUDIO_STATE_STOPPED;

/* Reconnection parameters */
#define BT_RECONNECT_MAX_ATTEMPTS 5
#define BT_RECONNECT_DELAY_MS 2000
static uint8_t s_reconnect_attempts = 0;
static bool s_auto_reconnect = true;
static uint32_t s_reconnect_delay_ms = BT_RECONNECT_DELAY_MS;
static char s_last_connected_addr[(ESP_BD_ADDR_LEN*2)+6] = {0}; // XX:XX:XX:XX:XX:XX format

#if CONFIG_BT_MOCK_TESTING
static esp_err_t s_test_reconnect_results[8];
static size_t s_test_reconnect_results_len;
static size_t s_test_reconnect_results_idx;
#endif

/* Callback functions */
static bt_connection_callback_t s_connection_callback = NULL;
static void *s_connection_callback_data = NULL;
static bt_stream_callback_t s_stream_callback = NULL;
static void *s_stream_callback_data = NULL;

/* Function declarations */
static void bt_connection_state_handler(esp_a2d_connection_state_t state, esp_bd_addr_t bd_addr);
static void bt_audio_state_handler(esp_a2d_audio_state_t state, esp_bd_addr_t bd_addr);
static void attempt_reconnection(void);
static esp_err_t initiate_connection(esp_bd_addr_t addr);
static void update_connection_state(bt_connection_state_t new_state);
static void update_streaming_state(bt_streaming_state_t new_state);
#ifdef UNIT_TEST
void bt_connection_manager_reset_state_for_test(void);
#endif

/*
 * A2DP connection state callback
 */
static void bt_connection_state_handler(esp_a2d_connection_state_t state, esp_bd_addr_t bd_addr)
{
    ESP_LOGI(TAG, "Connection state changed: %d", state);  // NOLINT(bugprone-branch-clone)
    s_a2d_conn_state = state;
    char addr_str[sizeof(s_last_connected_addr)] = {0};
    
    switch (state) {
        case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
            ESP_LOGI(TAG, "Device disconnected");  // NOLINT(bugprone-branch-clone)
            update_connection_state(BT_CONNECTION_STATE_DISCONNECTED);
            
            /* Save last connected address for reconnection */
            if (s_auto_reconnect && s_last_connected_addr[0] != '\0') {
                s_reconnect_attempts = 0;
                attempt_reconnection();
            }
            break;
            
        case ESP_A2D_CONNECTION_STATE_CONNECTING:
            ESP_LOGI(TAG, "Connecting to device");  // NOLINT(bugprone-branch-clone)
            update_connection_state(BT_CONNECTION_STATE_CONNECTING);
            break;
            
        case ESP_A2D_CONNECTION_STATE_CONNECTED:
            ESP_LOGI(TAG, "Connected to device");  // NOLINT(bugprone-branch-clone)
            util_format_mac(bd_addr, addr_str, sizeof(addr_str));
            safe_memset(s_last_connected_addr, sizeof(s_last_connected_addr), 0, sizeof(s_last_connected_addr));
            safe_memcpy(s_last_connected_addr, sizeof(s_last_connected_addr), addr_str, strlen(addr_str));
            safe_memset(s_connection_info.addr, sizeof(s_connection_info.addr), 0, sizeof(s_connection_info.addr));
            safe_memcpy(s_connection_info.addr, sizeof(s_connection_info.addr), addr_str, strlen(addr_str));
            s_connection_info.connect_time = (uint32_t)time(NULL);
            s_reconnect_attempts = 0;
            update_connection_state(BT_CONNECTION_STATE_CONNECTED);
            /* Persist last-connected MAC to NVS for auto-reconnect on next boot */
            {
                esp_err_t nvs_err = nvs_storage_set_last_connected_mac(s_last_connected_addr);
                if (nvs_err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to persist last-connected MAC to NVS: %d", nvs_err);  // NOLINT(bugprone-branch-clone)
                }
            }
            break;
            
        case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
            ESP_LOGI(TAG, "Disconnecting from device");  // NOLINT(bugprone-branch-clone)
            update_connection_state(BT_CONNECTION_STATE_DISCONNECTING);
            break;
            
        default:
            ESP_LOGW(TAG, "Unhandled A2DP connection state: %d", state);  // NOLINT(bugprone-branch-clone)
            break;
    }
    
    /* Notify application via callback */
    if (s_connection_callback) {
        bt_connection_info_t info;
        safe_memcpy(&info, sizeof(info), &s_connection_info, sizeof(bt_connection_info_t));
        s_connection_callback(&info, s_connection_callback_data);
    }
}

/* 
 * A2DP audio state callback
 */
static void bt_audio_state_handler(esp_a2d_audio_state_t state, esp_bd_addr_t bd_addr)
{
    ESP_LOGI(TAG, "Audio state changed: %d", state);  // NOLINT(bugprone-branch-clone)
    s_a2d_audio_state = state;
    
    switch (state) {
#if ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND != ESP_A2D_AUDIO_STATE_STOPPED
        case ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND:
            // Remote pause without full stop; keep connection but mark paused
            s_reconnect_delay_ms = BT_RECONNECT_DELAY_MS;
            update_streaming_state(BT_STREAMING_STATE_PAUSED);
            ESP_LOGI(TAG, "Audio streaming suspended by remote");
            break;
#endif

        case ESP_A2D_AUDIO_STATE_STOPPED: {
            /* When the enum values are identical we still want the
             * REMOTE_SUSPEND behavior. Treat STOPPED as a suspend if we
             * were streaming or if the enum equals REMOTE_SUSPEND. */
            bool remote_suspend = (state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) ||
                                  (s_streaming_state == BT_STREAMING_STATE_STREAMING) ||
                                  (s_streaming_state == BT_STREAMING_STATE_STARTING);
            if (remote_suspend) {
                s_reconnect_delay_ms = BT_RECONNECT_DELAY_MS;
                update_streaming_state(BT_STREAMING_STATE_PAUSED);
                ESP_LOGI(TAG, "Audio streaming suspended by remote");  // NOLINT(bugprone-branch-clone)
            } else {
                update_streaming_state(BT_STREAMING_STATE_STOPPED);
                ESP_LOGI(TAG, "Audio streaming stopped");  // NOLINT(bugprone-branch-clone)
            }
            break;
        }
            
        case ESP_A2D_AUDIO_STATE_STARTED:
            // When we first get started state, consider it as "starting"
            if (s_streaming_state != BT_STREAMING_STATE_STREAMING) {
                update_streaming_state(BT_STREAMING_STATE_STARTING);
                ESP_LOGI(TAG, "Audio streaming starting");  // NOLINT(bugprone-branch-clone)
                
                // After a short delay, transition to streaming state
                // In a real implementation, this would typically be handled by data flow
                platform_delay_ms(100);
                update_streaming_state(BT_STREAMING_STATE_STREAMING);
                ESP_LOGI(TAG, "Audio streaming started");  // NOLINT(bugprone-branch-clone)
            } else {
                ESP_LOGI(TAG, "Audio streaming continues");  // NOLINT(bugprone-branch-clone)
            }
            break;
            
        default:
            ESP_LOGW(TAG, "Unhandled A2DP audio state: %d", state);  // NOLINT(bugprone-branch-clone)
            break;
    }
    
    /* Notify application via callback */
    if (s_stream_callback) {
        s_stream_callback(s_streaming_state == BT_STREAMING_STATE_STREAMING, 
                         &s_streaming_info, s_stream_callback_data);
    }
}

/*
 * Attempt reconnection to last connected device
 */
static void attempt_reconnection(void)
{
    if (s_reconnect_attempts >= BT_RECONNECT_MAX_ATTEMPTS) {
        ESP_LOGI(TAG, "Max reconnection attempts reached");  // NOLINT(bugprone-branch-clone)
        return;
    }
    
    ESP_LOGI(TAG, "Attempting reconnection (%d/%d) to %s",  // NOLINT(bugprone-branch-clone)
             s_reconnect_attempts + 1, BT_RECONNECT_MAX_ATTEMPTS, s_last_connected_addr);

    /* Wait before trying to reconnect */
    if (s_reconnect_delay_ms > 0U) {
        platform_delay_ms(s_reconnect_delay_ms);
    }
    
    /* Attempt reconnection */
    esp_bd_addr_t addr;
    if (util_parse_mac(s_last_connected_addr, addr)) {
        if (initiate_connection(addr) == ESP_OK) {
            s_reconnect_attempts++;
            update_connection_state(BT_CONNECTION_STATE_CONNECTING);
        } else {
            /* Count the failed attempt before reporting state so retry_count matches attempts. */
            s_reconnect_attempts++;
            ESP_LOGE(TAG, "Failed to initiate reconnection");  // NOLINT(bugprone-branch-clone)
            update_connection_state(BT_CONNECTION_STATE_FAILED);
            
            /* Try again if retries remain */
            if (s_reconnect_attempts < BT_RECONNECT_MAX_ATTEMPTS) {
                attempt_reconnection();
            }
        }
    } else {
        ESP_LOGE(TAG, "Invalid device address format: %s", s_last_connected_addr);  // NOLINT(bugprone-branch-clone)
        update_connection_state(BT_CONNECTION_STATE_FAILED);
    }
}

/*
 * Update connection state and connection info
 */
static void update_connection_state(bt_connection_state_t new_state)
{
    s_connection_state = new_state;
    
    /* Update connected flag based on state */
    s_connection_info.connected = (new_state == BT_CONNECTION_STATE_CONNECTED);
    s_connection_info.state = new_state;
    
    /* Reset streaming state if disconnected */
    if (new_state == BT_CONNECTION_STATE_DISCONNECTED) {
        update_streaming_state(BT_STREAMING_STATE_STOPPED);
    }
    
    /* Update retry count */
    s_connection_info.retry_count = s_reconnect_attempts;
}

/*
 * Update streaming state and streaming info
 */
static void update_streaming_state(bt_streaming_state_t new_state)
{
    s_streaming_state = new_state;
    s_streaming_info.state = new_state;
    
    /* Update paused flag */
    s_streaming_info.paused = (new_state == BT_STREAMING_STATE_PAUSED);

    /* Reset streaming stats if stopped (CODE_REVIEW5 Task 3.1) */
    if (new_state == BT_STREAMING_STATE_STOPPED) {
        s_streaming_info.bytes_sent = 0;
        s_streaming_info.bytes_requested = 0;
        s_streaming_info.bytes_produced = 0;
        s_streaming_info.bytes_silence = 0;
        s_streaming_info.packets_sent = 0;
        s_streaming_info.packet_errors = 0;
        s_streaming_info.stream_duration = 0;
        /* CODE_REVIEW5 Task 3.2 */
        s_streaming_info.underrun_count = 0;
        s_streaming_info.total_callbacks = 0;
    }
}

/**
 * Public functions
 */

esp_err_t bt_register_connection_callback(bt_connection_callback_t callback, void* user_data)
{
    s_connection_callback = callback;
    s_connection_callback_data = user_data;
    return ESP_OK;
}

esp_err_t bt_register_streaming_callback(bt_stream_callback_t callback, void* user_data)
{
    s_stream_callback = callback;
    s_stream_callback_data = user_data;
    return ESP_OK;
}

bt_connection_state_t bt_get_connection_state_detailed(void)
{
    return s_connection_state;
}

int bt_get_connection_state(void)
{
    return (s_connection_state == BT_CONNECTION_STATE_CONNECTED) ? 1 : 0;
}

bt_streaming_state_t bt_get_streaming_state(void)
{
    return s_streaming_state;
}

int bt_get_streaming_state_int(void)
{
    return (s_streaming_state == BT_STREAMING_STATE_STREAMING) ? 1 : 0;
}

esp_err_t bt_get_connection_info(bt_connection_info_t* info)
{
    if (info == NULL) {
        return ESP_FAIL;
    }
    
    safe_memcpy(info, sizeof(*info), &s_connection_info, sizeof(bt_connection_info_t));
    return ESP_OK;
}

void bt_connection_manager_init(esp_a2d_cb_t conn_cb, esp_a2d_source_data_cb_t audio_cb)
{
    /* Register A2DP callbacks */
    esp_a2d_register_callback(conn_cb);
    esp_a2d_source_register_data_callback(audio_cb);
    
    /* Initialize connection info */
    safe_memset(&s_connection_info, sizeof(bt_connection_info_t), 0, sizeof(bt_connection_info_t));
    s_connection_info.state = BT_CONNECTION_STATE_DISCONNECTED;
    
    /* Initialize streaming info */
    safe_memset(&s_streaming_info, sizeof(bt_streaming_info_t), 0, sizeof(bt_streaming_info_t));
    s_streaming_info.state = BT_STREAMING_STATE_STOPPED;
    
    ESP_LOGI(TAG, "Connection manager initialized");  // NOLINT(bugprone-branch-clone)
}

/* Test hooks for reconnect sequencing (CONFIG_BT_MOCK_TESTING only) */
#if CONFIG_BT_MOCK_TESTING
static esp_err_t initiate_connection(esp_bd_addr_t addr)
{
    if (s_test_reconnect_results_len > 0U && s_test_reconnect_results_idx < s_test_reconnect_results_len) {
        return s_test_reconnect_results[s_test_reconnect_results_idx++];
    }
    return esp_a2d_source_connect(addr);
}

void bt_conn_test_set_reconnect_results(const esp_err_t *results, size_t len)
{
    if (results == NULL || len == 0U) {
        s_test_reconnect_results_len = 0;
        s_test_reconnect_results_idx = 0;
        return;
    }

    if (len > (sizeof(s_test_reconnect_results) / sizeof(s_test_reconnect_results[0]))) {
        len = sizeof(s_test_reconnect_results) / sizeof(s_test_reconnect_results[0]);
    }

    safe_memset(s_test_reconnect_results, sizeof(s_test_reconnect_results), 0, sizeof(s_test_reconnect_results));
    safe_memcpy(s_test_reconnect_results, sizeof(s_test_reconnect_results), results, len * sizeof(results[0]));
    s_test_reconnect_results_len = len;
    s_test_reconnect_results_idx = 0;
}

void bt_conn_test_set_reconnect_delay_ms(uint32_t delay_ms)
{
    s_reconnect_delay_ms = delay_ms;
}

void bt_conn_test_reset_state(void)
{
#ifdef UNIT_TEST
    bt_connection_manager_reset_state_for_test();
#else
    s_connection_state = BT_CONNECTION_STATE_DISCONNECTED;
    s_streaming_state = BT_STREAMING_STATE_STOPPED;
    safe_memset(&s_connection_info, sizeof(s_connection_info), 0, sizeof(s_connection_info));
    s_connection_info.state = BT_CONNECTION_STATE_DISCONNECTED;
    safe_memset(&s_streaming_info, sizeof(s_streaming_info), 0, sizeof(s_streaming_info));
    s_streaming_info.state = BT_STREAMING_STATE_STOPPED;
    s_a2d_conn_state = ESP_A2D_CONNECTION_STATE_DISCONNECTED;
    s_a2d_audio_state = ESP_A2D_AUDIO_STATE_STOPPED;
    s_reconnect_attempts = 0;
    s_auto_reconnect = true;
    s_reconnect_delay_ms = BT_RECONNECT_DELAY_MS;
    s_last_connected_addr[0] = '\0';
    s_connection_callback = NULL;
    s_connection_callback_data = NULL;
    s_stream_callback = NULL;
    s_stream_callback_data = NULL;
#endif
    s_test_reconnect_results_len = 0;
    s_test_reconnect_results_idx = 0;
}
#else
static esp_err_t initiate_connection(esp_bd_addr_t addr)
{
    return esp_a2d_source_connect(addr);
}
#endif

/* 
 * These functions are intended to be used as callbacks for the A2DP stack.
 * Adding exports to remove unused function warnings.
 */
void bt_connection_state_cb(esp_a2d_connection_state_t state, esp_bd_addr_t bd_addr)
{
    bt_connection_state_handler(state, bd_addr);
}

void bt_audio_state_cb(esp_a2d_audio_state_t state, esp_bd_addr_t bd_addr)
{
    bt_audio_state_handler(state, bd_addr);
}

#ifdef UNIT_TEST
void bt_connection_manager_reset_state_for_test(void)
{
    s_connection_state = BT_CONNECTION_STATE_DISCONNECTED;
    s_streaming_state = BT_STREAMING_STATE_STOPPED;
    safe_memset(&s_connection_info, sizeof(s_connection_info), 0, sizeof(s_connection_info));
    s_connection_info.state = BT_CONNECTION_STATE_DISCONNECTED;
    safe_memset(&s_streaming_info, sizeof(s_streaming_info), 0, sizeof(s_streaming_info));
    s_streaming_info.state = BT_STREAMING_STATE_STOPPED;
    s_a2d_conn_state = ESP_A2D_CONNECTION_STATE_DISCONNECTED;
    s_a2d_audio_state = ESP_A2D_AUDIO_STATE_STOPPED;
    s_reconnect_attempts = 0;
    s_auto_reconnect = true;
    s_reconnect_delay_ms = BT_RECONNECT_DELAY_MS;
    s_last_connected_addr[0] = '\0';
    s_connection_callback = NULL;
    s_connection_callback_data = NULL;
    s_stream_callback = NULL;
    s_stream_callback_data = NULL;
}

void bt_connection_manager_set_auto_reconnect_for_test(bool enable)
{
    s_auto_reconnect = enable;
}

const char *bt_connection_manager_get_last_connected_addr_for_test(void)
{
    return s_last_connected_addr;
}

uint8_t bt_connection_manager_get_reconnect_attempts_for_test(void)
{
    return s_reconnect_attempts;
}
#endif
