#ifndef ESP_BT_H
#define ESP_BT_H

/**
 * Mock version of the ESP-IDF Bluetooth API header
 * This provides minimal implementations needed for testing
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Status codes
typedef enum {
    ESP_BT_STATUS_SUCCESS = 0,
    ESP_BT_STATUS_FAIL,
    ESP_BT_STATUS_NOT_READY,
    ESP_BT_STATUS_NOMEM,
    ESP_BT_STATUS_BUSY,
    ESP_BT_STATUS_DONE,
    ESP_BT_STATUS_UNSUPPORTED,
    ESP_BT_STATUS_PARM_INVALID,
    ESP_BT_STATUS_UNHANDLED,
    ESP_BT_STATUS_AUTH_FAILURE,
    ESP_BT_STATUS_RMT_DEV_DOWN,
    ESP_BT_STATUS_AUTH_REJECTED
} esp_bt_status_t;

// Bluetooth modes
typedef enum {
    ESP_BT_MODE_IDLE = 0x00,
    ESP_BT_MODE_BLE = 0x01,
    ESP_BT_MODE_CLASSIC_BT = 0x02,
    ESP_BT_MODE_DUAL = 0x03
} esp_bt_mode_t;

// Bluetooth controller config
typedef struct {
    esp_bt_mode_t mode;
    uint8_t bt_max_acl_conn;
    uint8_t bt_max_sync_conn;
    bool auto_latency;
} esp_bt_controller_config_t;

// Default controller config macro
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() { \
    .mode = ESP_BT_MODE_CLASSIC_BT, \
    .bt_max_acl_conn = 3, \
    .bt_max_sync_conn = 1, \
    .auto_latency = false, \
}

// A2DP codec types
typedef enum {
    ESP_A2D_MCT_SBC = 0,
    ESP_A2D_MCT_M12,
    ESP_A2D_MCT_M24,
    ESP_A2D_MCT_ATRAC,
    ESP_A2D_MCT_NON_A2DP,
} esp_a2d_mct_t;

// A2DP connection states
typedef enum {
    ESP_A2D_CONNECTION_STATE_DISCONNECTED = 0,
    ESP_A2D_CONNECTION_STATE_CONNECTING,
    ESP_A2D_CONNECTION_STATE_CONNECTED,
    ESP_A2D_CONNECTION_STATE_DISCONNECTING
} esp_a2d_connection_state_t;

// A2DP audio states
typedef enum {
    ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND = 0,
    ESP_A2D_AUDIO_STATE_STOPPED,
    ESP_A2D_AUDIO_STATE_STARTED,
} esp_a2d_audio_state_t;

// A2DP media control type
typedef enum {
    ESP_A2D_MEDIA_CTRL_NONE = 0,
    ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY,
    ESP_A2D_MEDIA_CTRL_START,
    ESP_A2D_MEDIA_CTRL_STOP,
    ESP_A2D_MEDIA_CTRL_SUSPEND,
} esp_a2d_media_ctrl_t;

// Bluetooth device address
typedef struct {
    uint8_t addr[6];
} esp_bd_addr_t;

// Mock function prototypes
esp_bt_status_t esp_bt_controller_init(esp_bt_controller_config_t *cfg);
esp_bt_status_t esp_bt_controller_deinit(void);
esp_bt_status_t esp_bt_controller_enable(esp_bt_mode_t mode);
esp_bt_status_t esp_bt_controller_disable(void);

esp_bt_status_t esp_bluedroid_init(void);
esp_bt_status_t esp_bluedroid_deinit(void);
esp_bt_status_t esp_bluedroid_enable(void);
esp_bt_status_t esp_bluedroid_disable(void);

// A2DP source functions
esp_bt_status_t esp_a2d_source_init(void);
esp_bt_status_t esp_a2d_source_deinit(void);
esp_bt_status_t esp_a2d_source_connect(esp_bd_addr_t *remote_bda);
esp_bt_status_t esp_a2d_source_disconnect(esp_bd_addr_t *remote_bda);
esp_bt_status_t esp_a2d_media_ctrl(esp_a2d_media_ctrl_t ctrl);

// Testing helper functions
void esp_bt_gap_set_scan_mode(bool discoverable, bool connectable);
void esp_bt_mock_trigger_device_discovery(const char* name, const char* addr, int rssi);
void esp_bt_mock_trigger_connection_state_changed(esp_a2d_connection_state_t state, const char* addr);
void esp_bt_mock_trigger_audio_state_changed(esp_a2d_audio_state_t state);

// Mock GAP reply APIs used by pairing handlers
// Return values mimic esp_err_t semantics (0 = success)
int esp_bt_gap_pin_reply(uint8_t *bd_addr, bool accept, uint8_t pin_code_len, uint8_t *pin_code);
int esp_bt_gap_ssp_confirm_reply(uint8_t *bd_addr, bool accept);

// PIN code definitions (used by commands.c)
#ifndef ESP_BT_PIN_CODE_LEN
#define ESP_BT_PIN_CODE_LEN 16
#endif

typedef uint8_t esp_bt_pin_code_t[ESP_BT_PIN_CODE_LEN];

#ifdef __cplusplus
}
#endif

#endif // ESP_BT_H
