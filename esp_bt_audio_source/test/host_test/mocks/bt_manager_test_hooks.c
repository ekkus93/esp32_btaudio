#include <stdbool.h>
#include <string.h>

static char g_last_unpair_mac[18] = {0};

static int g_force_disconnect_failure = 0;
static int g_force_start_failure = 0;
static int g_force_stop_failure = 0;
static int g_force_unpair_failure = 0;

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

// Helper functions for tests
void bt_manager_test_set_force_disconnect_failure(int v) { g_force_disconnect_failure = v; }
void bt_manager_test_set_force_start_failure(int v) { g_force_start_failure = v; }
void bt_manager_test_set_force_stop_failure(int v) { g_force_stop_failure = v; }
void bt_manager_test_set_force_unpair_failure(int v) { g_force_unpair_failure = v; }

void bt_manager_test_reset_forces(void) {
    g_force_disconnect_failure = 0;
    g_force_start_failure = 0;
    g_force_stop_failure = 0;
    g_force_unpair_failure = 0;
    g_last_unpair_mac[0] = '\0';
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
