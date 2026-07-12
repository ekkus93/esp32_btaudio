/* bt_source_mock_internal.h — shared internals for the split bt_source_mock
 * translation units (core .c + a2dp/gap/scan/conn domain files). NOT a public
 * header: it centralizes the mock's includes, state (extern), typedefs and the
 * conditional bt_mock_* prototype block. */
#ifndef BT_SOURCE_MOCK_INTERNAL_H
#define BT_SOURCE_MOCK_INTERNAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <ctype.h>  // For tolower()
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_err.h"
#include "bt_source.h"
#include "bt_source_mock.h"
#include "bt_mock.h"
#include "bt_api.h"
#include "esp_a2dp_api.h"  /* bt_manager_test_invoke_a2dp_event */
#include "nvs_storage.h"
#include "nvs_flash.h"

/* Ensure device-level prototypes (scan/connect/get_scan_results, etc.) are
 * visible. bt_mock.h may not expose all device-level helpers; include the
 * device header which declares bt_mock_start_scan, bt_mock_stop_scan,
 * bt_mock_get_scan_results, bt_mock_connect and the connect-by-name hook.
 */
#include "bt_mock_devices.h"

#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations for component-level mock helpers so this compilation unit
 * has prototypes when delegating to bt_mock_* functions. These match the
 * declarations in components/bluetooth/include/bt_mock_devices.h.
 */
esp_err_t bt_mock_start_pairing(const char* addr);
esp_err_t bt_mock_send_pin(const char* pin);
esp_err_t bt_mock_unpair_all_devices(void);
esp_err_t bt_mock_unpair_device(const char* addr);
/* Prefer component-provided prototypes when available */
#include "bt_mock.h"

esp_err_t bt_mock_get_paired_devices(bt_device_t *devices, uint16_t max_count, uint16_t *actual_count);
esp_err_t bt_mock_get_default_pin(char* pin, size_t size);
/* Pairing state/method helpers and paired-device query provided by component mock */
bt_pairing_state_t bt_mock_get_pairing_state(void);
bt_pairing_method_t bt_mock_get_pairing_method(void);
bool bt_mock_is_device_paired(const char* addr);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
}
#endif
#endif


/* Constants */
#define MAX_DISCOVERED_DEVICES 32
#define MAX_STORED_PAIRED_DEVICES 32

/* Mock control structure for test results */
typedef struct {
    esp_err_t init_return;
    esp_err_t scan_start_return;
    esp_err_t connect_return;
    esp_err_t timeout_return;
    bt_device_t* paired_devices;
    int paired_device_count;
} mock_control_t;


/* Auto reconnect settings */
typedef struct {
    bool auto_reconnect_enabled;
    uint16_t retry_count;
    uint16_t retry_interval_ms;
} auto_reconnect_config_t;


/* Helper provided by bt_source_stubs.c */
extern void bt_source_stub_sync_connected_state(bool connected, const char* addr, const char* name);

/* Cross-file mock helpers (definitions kept in the core/scan TUs). */
bool is_valid_mac_address(const char* addr);
void cancel_scan_timer(void);

/* Shared mock state — definitions live in bt_source_mock.c (de-static'd). */
extern uint32_t s_diag_seq_mock;
extern bt_connection_state_t s_connection_state;
extern mock_control_t mock_control;
extern bool s_scan_active;
extern bt_device_t s_discovered_devices[MAX_DISCOVERED_DEVICES];
extern int s_discovered_device_count;
extern bt_device_type_t s_current_filter;
extern TimerHandle_t s_scan_timer;
extern bool s_connected;
extern bool s_defer_disconnect_visibility;
extern bool s_initialized;
extern bt_connection_info_t s_current_connection;
extern bt_profile_t s_active_profile;
extern bool s_streaming;
extern bool s_streaming_paused;
extern bt_streaming_state_t s_streaming_state;
extern bt_pairing_state_t current_pairing_state;
extern bt_pairing_method_t current_pairing_method;
extern char current_pairing_addr[18];
extern char default_pin[16];
extern bool pin_failure_simulation;
extern bool is_pairing;
extern bool s_ssp_support_enabled;
extern bool s_ssp_confirmation_requested;
extern char s_ssp_passkey[7];
extern uint32_t s_ssp_passkey_value;
extern bool s_device_paired[MAX_DISCOVERED_DEVICES];
extern int s_paired_device_count;
extern bt_device_t s_stored_paired_devices[MAX_STORED_PAIRED_DEVICES];
extern uint8_t s_stored_paired_device_count;
extern bool s_persistence_enabled;
extern bt_connection_callback_t s_connection_callback;
extern void* s_connection_callback_data;
extern auto_reconnect_config_t s_auto_reconnect_config;
extern uint8_t s_reconnect_attempts;

#if CONFIG_BT_MOCK_TESTING
extern esp_err_t s_test_reconnect_results[8];
extern size_t s_test_reconnect_results_len;
extern size_t s_test_reconnect_results_idx;
extern bool s_test_reconnect_delay_overridden;
extern uint32_t s_test_reconnect_delay_ms;
#endif

#endif /* BT_SOURCE_MOCK_INTERNAL_H */
