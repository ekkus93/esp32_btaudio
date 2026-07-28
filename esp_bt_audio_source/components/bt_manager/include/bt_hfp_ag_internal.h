#pragma once

#include "bt_hfp_ag.h"
#include "bt_duplex_state.h"
#include "platform_sync.h"

typedef struct {
    platform_mutex_t lock;
    platform_binary_sem_t completion;
    bool resources_ready;
    esp_err_t completion_result;
    bt_hfp_ag_snapshot_t snapshot;
#ifdef UNIT_TEST
    bt_hfp_ag_platform_ops_t test_ops;
    bool test_ops_set;
#endif
} bt_hfp_ag_context_t;

extern bt_hfp_ag_context_t g_bt_hfp_ag;

esp_err_t bt_hfp_ag_context_ensure(void);
esp_err_t bt_hfp_ag_lock(void);
esp_err_t bt_hfp_ag_unlock(esp_err_t prior);
void bt_hfp_ag_set_fault_locked(esp_err_t error);
void bt_hfp_ag_remember_peer_locked(const char *peer_mac);
esp_err_t bt_hfp_ag_platform_unknown_at_error(const char *peer_mac);

#ifdef ESP_PLATFORM
esp_err_t bt_hfp_ag_register_platform_callback(void);
#endif
