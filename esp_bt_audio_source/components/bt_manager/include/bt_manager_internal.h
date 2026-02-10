#pragma once

/**
 * bt_manager_internal.h - Internal shared declarations for bt_manager components
 * 
 * This header provides access to shared state and utilities for the bt_manager
 * subsystem modules (pairing, scan, connection, events). It should only be
 * included by implementation files within the bt_manager component, never by
 * external components.
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

