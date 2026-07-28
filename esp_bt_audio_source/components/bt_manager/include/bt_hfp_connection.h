#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_bt.h"
#include "bt_hfp_ag.h"
#include "bt_duplex_state_events.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_HFP_CONNECTION_REQUEST_TIMEOUT_MS 1000U
#define BT_HFP_CONNECTION_WATCHDOG_TIMEOUT_MS 10000U

typedef enum {
    BT_HFP_OPERATION_NONE = 0,
    BT_HFP_OPERATION_CONNECT,
    BT_HFP_OPERATION_DISCONNECT,
} bt_hfp_operation_type_t;

typedef enum {
    BT_HFP_OPERATION_IDLE = 0,
    BT_HFP_OPERATION_QUEUED,
    BT_HFP_OPERATION_REQUEST_SENT,
    BT_HFP_OPERATION_CONFIRMED,
    BT_HFP_OPERATION_REJECTED,
    BT_HFP_OPERATION_TIMED_OUT,
} bt_hfp_operation_state_t;

typedef struct {
    bt_hfp_operation_type_t type;
    bt_hfp_operation_state_t state;
    bool pending;
    uint32_t serial;
    uint32_t generation;
    char peer_mac[BT_HFP_AG_MAC_STR_LEN];
    uint64_t deadline_ms;
    esp_err_t immediate_result;
    esp_err_t last_error;
    uint64_t accepted_connect_requests;
    uint64_t accepted_disconnect_requests;
    uint64_t immediate_failures;
    uint64_t remote_rejections;
    uint64_t watchdog_timeouts;
    uint64_t stale_operation_events;
    uint64_t wrong_peer_events;
    uint64_t dispatch_failures;
} bt_hfp_connection_snapshot_t;

esp_err_t bt_hfp_connection_init(void);
esp_err_t bt_hfp_connection_cleanup_after_stack_shutdown(void);
esp_err_t bt_hfp_connection_get_snapshot(bt_hfp_connection_snapshot_t *out);

esp_err_t bt_hfp_connect(const char *mac);
esp_err_t bt_hfp_disconnect(void);

/* Returns ESP_OK when consumed by a tracked operation. Returns
 * ESP_ERR_NOT_FOUND when the general HFP event mapper should handle it. */
esp_err_t bt_hfp_connection_handle_event(
    const char *peer_mac,
    bt_hfp_ag_connection_state_t state);

#ifdef UNIT_TEST
typedef struct {
    esp_err_t (*slc_connect)(esp_bd_addr_t remote_bda);
    esp_err_t (*slc_disconnect)(esp_bd_addr_t remote_bda);
} bt_hfp_connection_platform_ops_t;

esp_err_t bt_hfp_connection_test_set_platform_ops(
    const bt_hfp_connection_platform_ops_t *ops);
void bt_hfp_connection_test_expire_watchdog(void);
#endif

#ifdef __cplusplus
}
#endif
