#pragma once

#include "bt_manager.h"
#include "bt_api.h"
#include "util_safe.h"
#include "platform_sync.h"  /* Platform shim for sync */
#include <stdbool.h>
#include <stdint.h>

/*
 * MAYBE_WEAK macro - Marks symbols as weak in unit test builds to allow
 * test overrides, while keeping them strong in production builds.
 * This avoids brittle #ifdef blocks that split function signatures across
 * preprocessor directives.
 */
#ifdef UNIT_TEST
#define MAYBE_WEAK __attribute__((weak))
#else
#define MAYBE_WEAK
#endif

#ifdef ESP_PLATFORM
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#else
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#endif

/* Main bt_manager context - shared across all modules */
typedef struct {
    bool initialized;
    char device_name[32];
    bool scanning;
    bool connected;
    /* Set by bt_connect() when a connection attempt is in progress;
     * cleared on CONNECTED or DISCONNECTED A2DP event.  Guards bt_disconnect()
     * from returning "already disconnected" during the ACL-up / A2DP-pending
     * window where BlueZ may show Connected=true but bt_ctx.connected is still false. */
    bool connecting;
    bool audio_playing;
    int volume;
    bt_device_list_t discovered_devices;
    bt_device_list_t paired_devices;
    bt_connected_cb connected_callback;
    bt_disconnected_cb disconnected_callback;
    char connected_mac[18];
    char connected_name[32];
} bt_manager_context_t;

extern bt_manager_context_t bt_ctx;
extern bool s_autostart_enabled;

#ifdef UNIT_TEST
/* Unit test tracking for auto-start attempts - accessed by event handlers */
extern int s_autostart_attempts;
#endif

/* Utility function aliases (from util_safe) */
#define safe_vsnprintf util_safe_vsnprintf
#define safe_snprintf util_safe_snprintf
#define safe_copy_str util_safe_copy_str
#define safe_memcpy util_safe_memcpy
#define safe_memset util_safe_memset
#define parse_mac_bytes util_parse_mac

/* ============================================================================
 * bt_ctx lock/unlock helpers
 *
 * Acquire s_bt_ctx_mutex before reading/writing bt_ctx fields.
 * Callbacks are NOT invoked while holding the lock.
 * ============================================================================ */
esp_err_t bt_ctx_lock(uint32_t timeout_ms);
void bt_ctx_unlock(void);

/* Test hooks for host-mode unit tests --------------------------------------
 *
 * Create/destroy the s_bt_ctx_mutex so that bt_ctx_lock()/bt_ctx_unlock()
 * succeed during unit tests that manipulate bt_ctx directly.
 */
#ifdef UNIT_TEST
esp_err_t bt_manager_test_init_mutex(void);
void bt_manager_test_deinit_mutex(void);
#endif
