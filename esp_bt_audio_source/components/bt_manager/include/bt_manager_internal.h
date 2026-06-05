#pragma once

/**
 * bt_manager_internal.h - Internal shared declarations for bt_manager components
 *
 * This header provides access to shared state and utilities for the bt_manager
 * subsystem modules (pairing, scan, connection, events). It should only be
 * included by implementation files within the bt_manager component, never by
 * external components.
 *
 * ─── STATE OWNERSHIP MAP ────────────────────────────────────────────────────
 *
 * bt_ctx (bt_manager.c)
 *   Canonical global state.  Covers: initialized flag, connected flag + addr,
 *   discovered_devices list, paired_devices list, scanning flag.
 *   Written ONLY from BtAppTask (or init path before task start).
 *   Read from other tasks ONLY via bt_manager_get_status() or the snapshot
 *   helpers bt_get_device_list_snapshot() / bt_get_paired_devices_snapshot().
 *
 * s_connection_info / s_streaming_info (bt_connection_manager.c)
 *   Connection-lifecycle state: state machine, connected address, streaming
 *   counters.  Updated by the A2DP event callbacks running on BtAppTask.
 *   Exposed to callers via bt_get_connection_info() (copy by value) and
 *   bt_get_streaming_info() (copy under spinlock).
 *
 * s_streaming_info / s_streaming_state (bt_streaming_manager.c)
 *   Streaming-specific state: bytes_sent/requested/produced, underrun counts.
 *   Updated inside the A2DP data callback (ISR-like context) under spinlock.
 *   Note: bt_connection_manager.c has its OWN s_streaming_info copy that
 *   tracks connection-level stats; bt_streaming_manager.c tracks the data-path
 *   stats.  These are NOT the same struct instance — prefer bt_streaming_manager
 *   as authoritative for audio bytes/underrun counters.
 *
 * ─── SYNCHRONIZATION RULES ──────────────────────────────────────────────────
 *
 *  Access to bt_ctx fields:  route through BtAppTask queue (bt_manager_get_status).
 *  Access to streaming stats: acquire s_stats_lock spinlock in bt_streaming_manager.c.
 *  Access to connection info:  call bt_get_connection_info() which copies under lock.
 *
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "bt_manager.h"
#include "bt_api.h"
#include "util_safe.h"
#include "platform_sync.h"  /* CODE_REVIEW8 P2.2 Phase 1: Platform shim for sync */
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

/* BT Manager Request/Response System (CODE_REVIEW8 P2: BT State Access Contract)
 * 
 * PURPOSE: Thread-safe access to bt_ctx from command handlers.
 * PROBLEM: bt_ctx updated from BtAppTask, read from cmd_proc without sync.
 * SOLUTION: Route all reads through BtAppTask via request/response queue.
 * 
 * USAGE:
 *   1. Create request with response buffer and semaphore
 *   2. Post to BtAppTask via bt_app_work_dispatch()
 *   3. Wait on semaphore (with timeout)
 *   4. Read response from buffer
 * 
 * See: code_review/BT_STATE_ACCESS_CONTRACT.md for design details.
 */

/* Request types for BT manager state queries */
typedef enum {
    BT_MGR_REQUEST_GET_STATUS = 0,      /* Get connection/audio status */
    BT_MGR_REQUEST_GET_STREAMING_INFO,  /* Get streaming statistics */
} bt_mgr_request_type_t;

/* Request message for thread-safe state access */
typedef struct {
    bt_mgr_request_type_t type;    /* Type of request */
    void *response_buf;             /* Caller-provided response buffer */
    size_t response_size;           /* Size of response buffer (for validation) */
    platform_binary_sem_t done_sem; /* Semaphore to signal completion (platform-agnostic) */
} bt_mgr_request_t;

/* Response structures for each request type */

/* Response for BT_MGR_REQUEST_GET_STATUS */
typedef struct {
    bool initialized;
    bool connected;
    bool audio_playing;
    bool scanning;
    char connected_mac[18];
    char connected_name[32];
} bt_mgr_status_response_t;

/* Response for BT_MGR_REQUEST_GET_STREAMING_INFO */
typedef struct {
    uint32_t total_callbacks;
    uint32_t underrun_count;
    uint32_t total_bytes_read;
} bt_mgr_streaming_info_response_t;

/* Request handler function - processes requests in BtAppTask context */
#ifdef ESP_PLATFORM
void bt_mgr_request_handler(uint16_t event, void *param);
#endif

/* Utility function aliases (from util_safe) */
#define safe_vsnprintf util_safe_vsnprintf
#define safe_snprintf util_safe_snprintf
#define safe_copy_str util_safe_copy_str
#define safe_memcpy util_safe_memcpy
#define safe_memset util_safe_memset
#define parse_mac_bytes util_parse_mac

