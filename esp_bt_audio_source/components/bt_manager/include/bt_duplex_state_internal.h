#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bt_duplex_state.h"
#include "platform_sync.h"

typedef struct {
    platform_mutex_t lock;
    bool initialized;
    uint64_t health_event_count;
#ifdef UNIT_TEST
    esp_err_t test_health_report_result;
#endif
    bt_duplex_snapshot_t snapshot;
} bt_duplex_context_t;

extern bt_duplex_context_t g_bt_duplex_ctx;

#define BT_DUPLEX_ENUM_VALUE_VALID(value, count) \
    (((int)(value) >= 0) && ((value) < (count)))

void bt_duplex_snapshot_defaults(bt_duplex_snapshot_t *snapshot);
bool bt_duplex_valid_mac(const char *mac);
bool bt_duplex_same_mac(const char *lhs, const char *rhs);
esp_err_t bt_duplex_lock(void);
esp_err_t bt_duplex_unlock_result(esp_err_t current_result);
bool bt_duplex_transient_resources_stopped_locked(void);
uint32_t bt_duplex_next_generation(uint32_t current);
esp_err_t bt_duplex_validate_event_locked(uint32_t generation,
                                          const char *peer_mac);
esp_err_t bt_duplex_illegal_locked(void);
void bt_duplex_record_event_delivery_failure(uint32_t generation,
                                             const char *peer_mac,
                                             esp_err_t error);
