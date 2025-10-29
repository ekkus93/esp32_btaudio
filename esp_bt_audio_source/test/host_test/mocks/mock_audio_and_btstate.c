/* Minimal mocks to satisfy linker for host-mode test builds.
 * Provides bt_get_connection_state() and audio_processor_beep()
 * so test targets that don't link the full audio or connection manager
 * can still build and run.
 */

#include <stdint.h>
#include "../../main/include/audio_processor.h"

// Track mock connection state for host tests. bt_manager mock code will call
// bt_manager_test_set_connection_state() to update this when it simulates
// a connection/disconnection.
static int s_mock_connected = 0;

void bt_manager_test_set_connection_state(int v) {
    s_mock_connected = v ? 1 : 0;
}

// Return 1 when mock connection established, 0 otherwise.
int bt_get_connection_state(void) {
    return s_mock_connected;
}

// Host-mode stub: record the request and return success. Non-blocking.
// Keep this minimal for host tests where real audio isn't required.
esp_err_t audio_processor_beep(uint32_t duration_ms) {
    (void)duration_ms;
    return ESP_OK;
}
