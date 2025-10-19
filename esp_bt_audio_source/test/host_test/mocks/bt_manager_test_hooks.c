#include <stdbool.h>

static int g_force_disconnect_failure = 0;
static int g_force_start_failure = 0;
static int g_force_stop_failure = 0;

int bt_manager_forced_disconnect_failure(void) {
    return g_force_disconnect_failure;
}

int bt_manager_forced_start_failure(void) {
    return g_force_start_failure;
}

int bt_manager_forced_stop_failure(void) {
    return g_force_stop_failure;
}

// Helper functions for tests
void bt_manager_test_set_force_disconnect_failure(int v) { g_force_disconnect_failure = v; }
void bt_manager_test_set_force_start_failure(int v) { g_force_start_failure = v; }
void bt_manager_test_set_force_stop_failure(int v) { g_force_stop_failure = v; }

void bt_manager_test_reset_forces(void) {
    g_force_disconnect_failure = 0;
    g_force_start_failure = 0;
    g_force_stop_failure = 0;
}
