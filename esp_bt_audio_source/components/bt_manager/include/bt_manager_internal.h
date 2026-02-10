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
#include <stdbool.h>
#include <stdint.h>

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

/* Utility function aliases (from util_safe) */
#define safe_vsnprintf util_safe_vsnprintf
#define safe_snprintf util_safe_snprintf
#define safe_copy_str util_safe_copy_str
#define safe_memcpy util_safe_memcpy
#define safe_memset util_safe_memset
#define parse_mac_bytes util_parse_mac

