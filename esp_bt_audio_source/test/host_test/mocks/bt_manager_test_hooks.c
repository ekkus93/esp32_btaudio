#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "bt_manager_internal.h"

static char g_last_unpair_mac[18] = {0};

static int g_force_disconnect_failure = 0;
static int g_force_start_failure = 0;
static int g_force_stop_failure = 0;
static int g_force_unpair_failure = 0;
static int g_force_unpair_all_failure = 0;
static int g_unpair_all_removed = 0;
static int g_unpair_all_cleared_before = 0;
static int g_unpair_all_temp_alloc_outstanding = 0;
static int g_unpair_all_temp_alloc_peak = 0;
static int g_scan_start_count = 0;
static int g_pair_event_count = 0;
static char g_last_pair_event_subtype[16] = {0};
static char g_last_pair_event_data[64] = {0};

int bt_manager_forced_disconnect_failure(void) {
    return g_force_disconnect_failure;
}

int bt_manager_forced_start_failure(void) {
    return g_force_start_failure;
}

int bt_manager_forced_stop_failure(void) {
    return g_force_stop_failure;
}

int bt_manager_test_should_force_unpair_failure(void) {
    return g_force_unpair_failure;
}

int bt_manager_test_should_force_unpair_all_failure(void) {
    return g_force_unpair_all_failure;
}

// Helper functions for tests
void bt_manager_test_set_force_disconnect_failure(int v) { g_force_disconnect_failure = v; }
void bt_manager_test_set_force_start_failure(int v) { g_force_start_failure = v; }
void bt_manager_test_set_force_stop_failure(int v) { g_force_stop_failure = v; }
void bt_manager_test_set_force_unpair_failure(int v) { g_force_unpair_failure = v; }
void bt_manager_test_set_force_unpair_all_failure(int v) { g_force_unpair_all_failure = v; }

void bt_manager_test_reset_forces(void) {
    g_force_disconnect_failure = 0;
    g_force_start_failure = 0;
    g_force_stop_failure = 0;
    g_force_unpair_failure = 0;
    g_force_unpair_all_failure = 0;
    g_last_unpair_mac[0] = '\0';
    g_unpair_all_removed = 0;
    g_unpair_all_cleared_before = 0;
    g_unpair_all_temp_alloc_outstanding = 0;
    g_unpair_all_temp_alloc_peak = 0;
    g_scan_start_count = 0;
    g_pair_event_count = 0;
    g_last_pair_event_subtype[0] = '\0';
    g_last_pair_event_data[0] = '\0';
    
    // Reset bt_ctx state that affects subsequent tests
    bt_ctx.scanning = false;
}

void bt_manager_test_record_unpair(const char* mac) {
    if (!mac) {
        g_last_unpair_mac[0] = '\0';
        return;
    }
    strncpy(g_last_unpair_mac, mac, sizeof(g_last_unpair_mac) - 1);
    g_last_unpair_mac[sizeof(g_last_unpair_mac) - 1] = '\0';
}

const char* bt_manager_test_get_last_unpair_mac(void) {
    return g_last_unpair_mac;
}

void bt_manager_test_record_unpair_all_call(int cleared_before, int removed) {
    g_unpair_all_cleared_before = cleared_before;
    g_unpair_all_removed = removed;
}

void bt_manager_test_record_unpair_all_temp_alloc(int delta) {
    g_unpair_all_temp_alloc_outstanding += delta;
    if (g_unpair_all_temp_alloc_outstanding > g_unpair_all_temp_alloc_peak) {
        g_unpair_all_temp_alloc_peak = g_unpair_all_temp_alloc_outstanding;
    }
}

// Record that a scan was started (called from bt_manager in unit tests)
void bt_manager_test_record_scan_start(void) {
    g_scan_start_count++;
}

int bt_manager_test_get_scan_start_count(void) {
    return g_scan_start_count;
}

void bt_manager_test_record_pair_event(const char* subtype, const char* data) {
    g_pair_event_count++;
    if (subtype) {
        strncpy(g_last_pair_event_subtype, subtype, sizeof(g_last_pair_event_subtype) - 1);
        g_last_pair_event_subtype[sizeof(g_last_pair_event_subtype) - 1] = '\0';
    }
    if (data) {
        strncpy(g_last_pair_event_data, data, sizeof(g_last_pair_event_data) - 1);
        g_last_pair_event_data[sizeof(g_last_pair_event_data) - 1] = '\0';
    }
}

int bt_manager_test_get_pair_event_count(void) {
    return g_pair_event_count;
}

const char* bt_manager_test_get_last_pair_event_subtype(void) {
    return g_last_pair_event_subtype;
}

const char* bt_manager_test_get_last_pair_event_data(void) {
    return g_last_pair_event_data;
}

int bt_manager_test_get_unpair_all_removed(void) {
    return g_unpair_all_removed;
}

int bt_manager_test_get_unpair_all_cleared_before(void) {
    return g_unpair_all_cleared_before;
}

int bt_manager_test_get_unpair_all_temp_alloc_outstanding(void) {
    return g_unpair_all_temp_alloc_outstanding;
}

int bt_manager_test_get_unpair_all_temp_alloc_peak(void) {
    return g_unpair_all_temp_alloc_peak;
}
