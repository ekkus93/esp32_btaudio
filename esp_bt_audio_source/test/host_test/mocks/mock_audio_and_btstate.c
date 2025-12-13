/* Minimal mocks to satisfy linker for host-mode test builds.
 * Provides bt_get_connection_state() and audio_processor_beep()
 * so test targets that don't link the full audio or connection manager
 * can still build and run.
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "bt_api.h"
#include "../../main/include/audio_processor.h"

// Track mock connection state for host tests. bt_manager mock code will call
// bt_manager_test_set_connection_state() to update this when it simulates
// a connection/disconnection.
static int s_mock_connected = 0;
static bool s_beep_active = false;

void bt_manager_test_set_connection_state(int v) {
    s_mock_connected = v ? 1 : 0;
}

// Return 1 when mock connection established, 0 otherwise.
__attribute__((weak)) int bt_get_connection_state(void) {
    return s_mock_connected;
}

__attribute__((weak)) int bt_manager_is_connected(void) {
    return s_mock_connected;
}

__attribute__((weak)) int bt_get_streaming_state_int(void) {
    return 0; /* host tests default to not streaming */
}

__attribute__((weak)) bt_err_t bt_start_audio(void) {
    /* Pretend start succeeds; host tests don't simulate full stack */
    return ESP_OK;
}



