/* Minimal mocks to satisfy linker for host-mode test builds.
 * Provides bt_get_connection_state() and audio_processor_beep()
 * so test targets that don't link the full audio or connection manager
 * can still build and run.
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "bt_api.h"
#include "esp_bt.h"
#include "../../components/audio_processor/include/audio_processor.h"

// Track mock connection state for host tests. bt_manager mock code will call
// bt_manager_test_set_connection_state() to update this when it simulates
// a connection/disconnection.
static int s_mock_connected = 0;
static bool s_beep_active = false;
static int s_start_audio_calls = 0;
static int s_last_conn_state = -1;
static int s_last_audio_state = -1;
static bool s_play_active = false;

void bt_manager_test_reset_btstate_mock(void) {
    s_mock_connected = 0;
    s_beep_active = false;
    s_start_audio_calls = 0;
    s_last_conn_state = -1;
    s_last_audio_state = -1;
    s_play_active = false;
}

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
    s_start_audio_calls++;
    /* Pretend start succeeds; host tests don't simulate full stack */
    return ESP_OK;
}

int bt_manager_test_get_start_audio_calls(void) {
    return s_start_audio_calls;
}

int bt_manager_test_get_last_conn_state(void) {
    return s_last_conn_state;
}

int bt_manager_test_get_last_audio_state(void) {
    return s_last_audio_state;
}

void play_manager_test_set_active(bool active) {
    s_play_active = active;
}

__attribute__((weak)) bool play_manager_is_active(void) {
    return s_play_active;
}

/* Capture forwarded callbacks from bt_manager when host builds supply them. */
void bt_connection_state_cb(esp_a2d_connection_state_t state, esp_bd_addr_t bd_addr) {
    (void)bd_addr;
    s_last_conn_state = (int)state;
}

void bt_audio_state_cb(esp_a2d_audio_state_t state, esp_bd_addr_t bd_addr) {
    (void)bd_addr;
    s_last_audio_state = (int)state;
}



