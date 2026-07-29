/* Minimal mocks to satisfy linker for host-mode test builds.
 * Provides bt_get_connection_state() and audio_processor_beep()
 * so test targets that don't link the full audio or connection manager
 * can still build and run.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"
#include "bt_api.h"
#include "esp_bt.h"
#include "command_interface.h"
#include "bt_duplex_policy.h"
#include "bt_duplex_state_events.h"
#include "bt_hfp_manager.h"
#include "../../components/audio_processor/include/audio_processor.h"
/* CODE_REVIEW5 Task 3.1: Need bt_streaming_info_t for stub */
#include "bt_manager.h"  /* Defines bt_device_t */
#define BT_SOURCE_SKIP_DEVICE_STRUCT 1
#include "bt_source.h"   /* Defines bt_streaming_info_t */
#undef BT_SOURCE_SKIP_DEVICE_STRUCT

// Track mock connection state for host tests. bt_manager mock code will call
// bt_manager_test_set_connection_state() to update this when it simulates
// a connection/disconnection.
static int s_mock_connected = 0;
static bool s_beep_active = false;
static int s_start_audio_calls = 0;
static int s_last_conn_state = -1;
static int s_last_audio_state = -1;
static bool s_force_streaming_info_failure = false;  /* CODE_REVIEW8 Task B */
static bt_connection_info_t s_mock_conn_info;        /* zeroed => not connected */

static bt_hfp_manager_status_t s_hfp_status;
static esp_err_t s_hfp_status_result = ESP_OK;
static esp_err_t s_hfp_profile_result = ESP_ERR_NOT_FOUND;
static esp_err_t s_hfp_audio_result = ESP_ERR_NOT_FOUND;
static uint32_t s_profile_created_generation;
static unsigned s_hfp_status_calls;
static unsigned s_hfp_profile_calls;
static unsigned s_hfp_audio_calls;
static uint32_t s_last_hfp_profile_generation;
static uint32_t s_last_hfp_audio_generation;
static bt_a2dp_profile_state_t s_last_hfp_profile_state;
static bt_a2dp_audio_state_t s_last_hfp_audio_policy_state;
static char s_last_hfp_profile_peer[BT_DUPLEX_MAC_STR_LEN];
static char s_last_hfp_audio_peer[BT_DUPLEX_MAC_STR_LEN];
static unsigned s_stale_operation_records;
static uint32_t s_last_stale_generation;
static char s_last_stale_peer[BT_DUPLEX_MAC_STR_LEN];

void bt_manager_test_reset_btstate_mock(void) {
    s_mock_connected = 0;
    s_beep_active = false;
    s_start_audio_calls = 0;
    s_last_conn_state = -1;
    s_last_audio_state = -1;
    s_force_streaming_info_failure = false;  /* CODE_REVIEW8 Task B */
    memset(&s_mock_conn_info, 0, sizeof(s_mock_conn_info));
    memset(&s_hfp_status, 0, sizeof(s_hfp_status));
    s_hfp_status.manager_initialized = true;
    s_hfp_status.configured_mode = BT_DUPLEX_MODE_DISABLED;
    s_hfp_status_result = ESP_OK;
    s_hfp_profile_result = ESP_ERR_NOT_FOUND;
    s_hfp_audio_result = ESP_ERR_NOT_FOUND;
    s_profile_created_generation = 0U;
    s_hfp_status_calls = 0U;
    s_hfp_profile_calls = 0U;
    s_hfp_audio_calls = 0U;
    s_last_hfp_profile_generation = 0U;
    s_last_hfp_audio_generation = 0U;
    s_last_hfp_profile_state = BT_A2DP_PROFILE_DISCONNECTED;
    s_last_hfp_audio_policy_state = BT_A2DP_AUDIO_STOPPED;
    memset(s_last_hfp_profile_peer, 0, sizeof(s_last_hfp_profile_peer));
    memset(s_last_hfp_audio_peer, 0, sizeof(s_last_hfp_audio_peer));
    s_stale_operation_records = 0U;
    s_last_stale_generation = 0U;
    memset(s_last_stale_peer, 0, sizeof(s_last_stale_peer));
}

void bt_manager_test_set_hfp_policy_status(bool peer_valid,
                                           const char *peer,
                                           uint32_t generation,
                                           esp_err_t result) {
    memset(&s_hfp_status, 0, sizeof(s_hfp_status));
    s_hfp_status.manager_initialized = true;
    s_hfp_status.configured_mode = BT_DUPLEX_MODE_DISABLED;
    s_hfp_status.duplex.peer_valid = peer_valid;
    s_hfp_status.duplex.session_generation = generation;
    if (peer_valid && peer != NULL) {
        strncpy(s_hfp_status.duplex.peer_mac, peer,
                sizeof(s_hfp_status.duplex.peer_mac) - 1U);
    }
    s_hfp_status_result = result;
}

void bt_manager_test_set_hfp_policy_results(esp_err_t profile_result,
                                             esp_err_t audio_result) {
    s_hfp_profile_result = profile_result;
    s_hfp_audio_result = audio_result;
}

void bt_manager_test_set_hfp_profile_created_generation(uint32_t generation) {
    s_profile_created_generation = generation;
}

unsigned bt_manager_test_get_hfp_status_calls(void) {
    return s_hfp_status_calls;
}

unsigned bt_manager_test_get_hfp_profile_calls(void) {
    return s_hfp_profile_calls;
}

unsigned bt_manager_test_get_hfp_audio_policy_calls(void) {
    return s_hfp_audio_calls;
}

uint32_t bt_manager_test_get_last_hfp_profile_generation(void) {
    return s_last_hfp_profile_generation;
}

uint32_t bt_manager_test_get_last_hfp_audio_generation(void) {
    return s_last_hfp_audio_generation;
}

unsigned bt_manager_test_get_stale_operation_records(void) {
    return s_stale_operation_records;
}

/* CODE_REVIEW8 Task B: Test helper to force bt_get_streaming_info failure */
void bt_manager_test_force_streaming_info_failure(bool force) {
    s_force_streaming_info_failure = force;
}

void bt_manager_test_set_connection_state(int v) {
    s_mock_connected = v ? 1 : 0;
}

/* Connected-peer info stub: lets STATUS tests exercise the CONN_MAC field the
 * S3 reads to show which A2DP sink is on the link. Zeroed => not connected. */
void bt_manager_test_set_connection_info(bool connected, const char *mac) {
    memset(&s_mock_conn_info, 0, sizeof(s_mock_conn_info));
    s_mock_conn_info.connected = connected;
    if (mac) {
        strncpy(s_mock_conn_info.addr, mac, sizeof(s_mock_conn_info.addr) - 1);
    }
}

__attribute__((weak)) esp_err_t bt_get_connection_info(bt_connection_info_t *info) {
    if (info == NULL) return ESP_FAIL;
    *info = s_mock_conn_info;
    return ESP_OK;
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

/* CODE_REVIEW5 Task 3.1: Stub for bt_get_streaming_info() */
/* CODE_REVIEW8 Task B: Enhanced to support failure injection for testing */
__attribute__((weak)) esp_err_t bt_get_streaming_info(bt_streaming_info_t* info) {
    if (info == NULL) {
        return ESP_FAIL;
    }
    /* CODE_REVIEW8 Task B: Allow tests to force failure */
    if (s_force_streaming_info_failure) {
        return ESP_FAIL;
    }
    /* Return zeroed streaming info for host tests */
    info->state = BT_STREAMING_STATE_STOPPED;
    info->bytes_sent = 0;
    info->bytes_requested = 0;
    info->bytes_produced = 0;
    info->bytes_silence = 0;
    info->packets_sent = 0;
    info->packet_errors = 0;
    info->stream_duration = 0;
    info->paused = false;
    /* CODE_REVIEW5 Task 3.2 */
    info->underrun_count = 0;
    info->total_callbacks = 0;
    return ESP_OK;
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

/* Generic host targets intentionally do not link the FD-11/FD-16 manager
 * facades. The HFP handler fails closed, while A2DP policy callbacks report
 * ESP_ERR_NOT_FOUND so the legacy event path can proceed without inventing an
 * authoritative duplex session. Focused FD-16 tests link the real policy. */
cmd_status_t cmd_handle_hfp(const cmd_context_t *ctx) {
    (void)ctx;
    return CMD_ERROR_NOT_INITIALIZED;
}

__attribute__((weak)) esp_err_t bt_manager_hfp_get_status(
    bt_hfp_manager_status_t *out) {
    s_hfp_status_calls++;
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (s_hfp_status_result != ESP_OK) return s_hfp_status_result;
    *out = s_hfp_status;
    return ESP_OK;
}

__attribute__((weak)) esp_err_t bt_manager_hfp_handle_a2dp_profile_event(
    uint32_t expected_generation, const char *peer_mac,
    bt_a2dp_profile_state_t state) {
    s_hfp_profile_calls++;
    s_last_hfp_profile_generation = expected_generation;
    s_last_hfp_profile_state = state;
    if (peer_mac != NULL) {
        strncpy(s_last_hfp_profile_peer, peer_mac,
                sizeof(s_last_hfp_profile_peer) - 1U);
    }
    if (s_hfp_profile_result == ESP_OK &&
        expected_generation == 0U &&
        !s_hfp_status.duplex.peer_valid &&
        peer_mac != NULL &&
        s_profile_created_generation != 0U &&
        state != BT_A2DP_PROFILE_DISCONNECTED &&
        state != BT_A2DP_PROFILE_DISCONNECTING) {
        s_hfp_status.duplex.peer_valid = true;
        s_hfp_status.duplex.session_generation =
            s_profile_created_generation;
        strncpy(s_hfp_status.duplex.peer_mac, peer_mac,
                sizeof(s_hfp_status.duplex.peer_mac) - 1U);
    }
    return s_hfp_profile_result;
}

__attribute__((weak)) esp_err_t bt_manager_hfp_handle_a2dp_audio_event(
    uint32_t expected_generation, const char *peer_mac,
    bt_a2dp_audio_state_t state) {
    s_hfp_audio_calls++;
    s_last_hfp_audio_generation = expected_generation;
    s_last_hfp_audio_policy_state = state;
    if (peer_mac != NULL) {
        strncpy(s_last_hfp_audio_peer, peer_mac,
                sizeof(s_last_hfp_audio_peer) - 1U);
    }
    return s_hfp_audio_result;
}

__attribute__((weak)) esp_err_t bt_duplex_record_stale_operation_event(
    uint32_t generation, const char *peer_mac) {
    s_stale_operation_records++;
    s_last_stale_generation = generation;
    if (peer_mac != NULL) {
        strncpy(s_last_stale_peer, peer_mac,
                sizeof(s_last_stale_peer) - 1U);
    }
    return ESP_OK;
}

/* Focused manager lifecycle tests link production bt_manager.c without the HFP
 * lifecycle and duplex-state objects. Weak no-op stubs model the already-stopped
 * lower stack; targets that link the real implementations override them. */
__attribute__((weak)) void bt_hfp_ag_force_cleanup_after_stack_shutdown(void) {
}

__attribute__((weak)) void bt_duplex_state_deinit(void) {
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
