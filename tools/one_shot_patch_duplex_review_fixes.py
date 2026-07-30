#!/usr/bin/env python3
"""Apply the bounded duplex stabilization review fixes.

This one-shot script is executed by a temporary workflow and deleted in the
same commit as the durable changes. Every replacement is exact and fails
closed if the expected baseline text is absent or ambiguous.
"""

from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(
            f"expected exactly one match in {path}, found {count}: {old[:120]!r}"
        )
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


def insert_before_once(path: str, marker: str, addition: str) -> None:
    replace_once(path, marker, addition + marker)


# FIX-01: enforce the command sanitizer and retain nested evidence logs.
replace_once(
    ".github/workflows/ci-host-tests.yml",
    """      - name: Run FD-16 duplex policy sanitizer tests
        run: bash esp_bt_audio_source/tools/run_bt_duplex_policy_test.sh

      - name: Run A2DP binding lifecycle sanitizer tests
""",
    """      - name: Run FD-16 duplex policy sanitizer tests
        run: bash esp_bt_audio_source/tools/run_bt_duplex_policy_test.sh

      - name: Run HFP command sanitizer tests
        run: bash esp_bt_audio_source/tools/run_bt_hfp_commands_test.sh

      - name: Run A2DP binding lifecycle sanitizer tests
""",
)
replace_once(
    ".github/workflows/ci-host-tests.yml",
    """            esp_bt_audio_source/test/host_test/build_host_tests/*.log
            esp_bt_audio_source/test/host_test/build_host_tests/cmake-targets.txt
""",
    """            esp_bt_audio_source/test/host_test/build_host_tests/*.log
            esp_bt_audio_source/test/host_test/build_host_tests/**/*.log
            esp_bt_audio_source/test/host_test/build_host_tests/cmake-targets.txt
""",
)

# FIX-02/FIX-03/FIX-04: production visibility and explicit precedence.
a2dp = "esp_bt_audio_source/components/bt_manager/bt_events_a2dp.c"
replace_once(
    a2dp,
    """#define TAG "BT_EVT_A2DP"
#define A2DP_BINDING_MAC_STR_LEN 18U
""",
    """#define TAG "BT_EVT_A2DP"
#define A2DP_BINDING_MAC_STR_LEN 18U
#define A2DP_DATA_ERROR_LOG_INTERVAL 64U
""",
)
replace_once(
    a2dp,
    """typedef struct {
    char peer_mac[A2DP_BINDING_MAC_STR_LEN];
    esp_a2d_conn_hdl_t conn_handle;
    uint32_t lifecycle_serial;
    uint32_t generation;
    bt_a2dp_audio_state_t state;
} a2dp_bound_audio_event_t;

/* Guarded by bt_ctx.lock. The ESP-IDF connection handle is the event-owned
""",
    """typedef struct {
    char peer_mac[A2DP_BINDING_MAC_STR_LEN];
    esp_a2d_conn_hdl_t conn_handle;
    uint32_t lifecycle_serial;
    uint32_t generation;
    bt_a2dp_audio_state_t state;
} a2dp_bound_audio_event_t;

typedef struct {
    uint64_t audio_read_failures;
    esp_err_t last_audio_read_error;
    uint32_t suppressed_audio_read_error_logs;
} a2dp_data_diagnostics_t;

/* The ESP-IDF A2DP source invokes its data callback serially. Only that
 * callback writes this diagnostic state in production; UNIT_TEST snapshots are
 * taken after synchronous callback return. Avoiding the manager mutex keeps the
 * real-time callback nonblocking and allocation-free. */
static a2dp_data_diagnostics_t s_a2dp_data_diag;

/* Guarded by bt_ctx.lock. The ESP-IDF connection handle is the event-owned
""",
)
replace_once(
    a2dp,
    """static esp_err_t s_test_last_generation_diag_update_error = ESP_OK;
static esp_err_t s_test_last_binding_clear_error = ESP_OK;
#endif
""",
    """static esp_err_t s_test_last_generation_diag_update_error = ESP_OK;
static esp_err_t s_test_last_binding_clear_error = ESP_OK;
static esp_err_t s_test_last_stale_record_error = ESP_OK;
static esp_err_t s_test_last_unbound_status_error = ESP_OK;
static esp_err_t s_test_last_connection_policy_error = ESP_OK;
#endif
""",
)
replace_once(
    a2dp,
    """static void record_rejected_bound_event(uint32_t generation,
                                        const char *event_peer)
{
    if (generation != 0U && event_peer != NULL) {
        (void)bt_duplex_record_stale_operation_event(generation, event_peer);
    }
}

static void record_rejected_unbound_event(const char *event_peer)
{
    bt_hfp_manager_status_t status;
    if (event_peer != NULL &&
        bt_manager_hfp_get_status(&status) == ESP_OK &&
        status.duplex.peer_valid &&
        status.duplex.session_generation != 0U) {
        (void)bt_duplex_record_stale_operation_event(
            status.duplex.session_generation, event_peer);
    }
}
""",
    """static esp_err_t record_rejected_bound_event(uint32_t generation,
                                                const char *event_peer,
                                                const char *reason)
{
    if (generation == 0U || event_peer == NULL) {
#ifdef UNIT_TEST
        s_test_last_stale_record_error = ESP_ERR_NOT_FOUND;
#endif
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = bt_duplex_record_stale_operation_event(
        generation, event_peer);
#ifdef UNIT_TEST
    s_test_last_stale_record_error = err;
#endif
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "A2DP stale-operation telemetry failed: peer=%s generation=%u reason=%s error=%s",
                 event_peer,
                 (unsigned)generation,
                 reason != NULL ? reason : "UNKNOWN",
                 esp_err_to_name(err));
    }
    return err;
}

static esp_err_t record_rejected_unbound_event(const char *event_peer,
                                                const char *reason)
{
    if (event_peer == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    bt_hfp_manager_status_t status;
    esp_err_t status_err = bt_manager_hfp_get_status(&status);
#ifdef UNIT_TEST
    s_test_last_unbound_status_error = status_err;
#endif
    if (status_err != ESP_OK) {
        ESP_LOGE(TAG,
                 "A2DP unbound stale-operation status lookup failed: peer=%s reason=%s error=%s",
                 event_peer,
                 reason != NULL ? reason : "UNKNOWN",
                 esp_err_to_name(status_err));
        return status_err;
    }
    if (!status.duplex.peer_valid ||
        status.duplex.session_generation == 0U) {
        return ESP_ERR_NOT_FOUND;
    }

    return record_rejected_bound_event(
        status.duplex.session_generation, event_peer, reason);
}
""",
)
# Give every telemetry attempt a stable diagnostic reason.
replacements = {
    "record_rejected_bound_event(generation, bound->peer_mac);": [
        "record_rejected_bound_event(\n                generation, bound->peer_mac, \"PROFILE_WRONG_PEER\");",
        "record_rejected_bound_event(\n                generation, bound->peer_mac, \"PROFILE_STALE_HANDLE\");",
        "record_rejected_bound_event(\n                generation, bound->peer_mac, \"AUDIO_WRONG_PEER\");",
        "record_rejected_bound_event(\n                generation, bound->peer_mac, \"AUDIO_STALE_HANDLE\");",
    ],
}
text = Path(a2dp).read_text(encoding="utf-8")
needle = "record_rejected_bound_event(generation, bound->peer_mac);"
if text.count(needle) != 4:
    raise RuntimeError(f"expected four generation/bound rejection calls, found {text.count(needle)}")
for replacement in replacements[needle]:
    text = text.replace(needle, replacement, 1)
Path(a2dp).write_text(text, encoding="utf-8")
replace_once(
    a2dp,
    "record_rejected_unbound_event(bound->peer_mac);",
    "record_rejected_unbound_event(\n            bound->peer_mac, \"PROFILE_NO_ACTIVE_BINDING\");",
)
# There are two remaining unbound calls: generation refresh and audio capture.
text = Path(a2dp).read_text(encoding="utf-8")
needle = "record_rejected_unbound_event(peer);"
if text.count(needle) != 1:
    raise RuntimeError(f"expected one generation unbound call, found {text.count(needle)}")
text = text.replace(
    needle,
    "record_rejected_unbound_event(\n            peer, \"GENERATION_NO_ACTIVE_SESSION\");",
    1,
)
needle = "record_rejected_unbound_event(bound->peer_mac);"
if text.count(needle) != 1:
    raise RuntimeError(f"expected one audio unbound call, found {text.count(needle)}")
text = text.replace(
    needle,
    "record_rejected_unbound_event(\n            bound->peer_mac, \"AUDIO_NO_ACTIVE_BINDING\");",
    1,
)
Path(a2dp).write_text(text, encoding="utf-8")
replace_once(
    a2dp,
    "record_rejected_bound_event(fallback_generation, peer);",
    "record_rejected_bound_event(\n                fallback_generation, peer, \"GENERATION_PEER_MISMATCH\");",
)
replace_once(
    a2dp,
    "record_rejected_bound_event(generation, peer);",
    "record_rejected_bound_event(\n            generation, peer, \"GENERATION_BINDING_CHANGED\");",
)
# Policy-level hard errors remain observable too.
text = Path(a2dp).read_text(encoding="utf-8")
needle = "record_rejected_bound_event(bound->generation, bound->peer_mac);"
if text.count(needle) != 2:
    raise RuntimeError(f"expected two policy rejection calls, found {text.count(needle)}")
text = text.replace(
    needle,
    "record_rejected_bound_event(\n            bound->generation, bound->peer_mac, \"PROFILE_POLICY_REJECTED\");",
    1,
)
text = text.replace(
    needle,
    "record_rejected_bound_event(\n            bound->generation, bound->peer_mac, \"AUDIO_POLICY_REJECTED\");",
    1,
)
Path(a2dp).write_text(text, encoding="utf-8")
replace_once(
    a2dp,
    """        if ((err == ESP_OK || err == ESP_ERR_NOT_FOUND) &&
            clear_err != ESP_OK) {
            err = clear_err;
        }
    }
    report_policy_result("A2DP connection", err);
}
""",
    """        /* ESP_ERR_NOT_FOUND here is the idempotent "no duplex session"
         * policy result, not a hard primary failure. A real binding-clear
         * failure is more actionable and must remain visible. Hard policy
         * errors remain authoritative. */
        if ((err == ESP_OK || err == ESP_ERR_NOT_FOUND) &&
            clear_err != ESP_OK) {
            err = clear_err;
        }
    }
#ifdef UNIT_TEST
    s_test_last_connection_policy_error = err;
#endif
    report_policy_result("A2DP connection", err);
}
""",
)
insert_before_once(
    a2dp,
    "#ifdef ESP_PLATFORM\n// Updated to match esp_a2d_source_data_cb_t: fill buffer and return bytes written\n",
    """static void a2dp_data_record_audio_read_failure(esp_err_t err,
                                                   size_t requested)
{
    if (s_a2dp_data_diag.audio_read_failures != UINT64_MAX) {
        s_a2dp_data_diag.audio_read_failures++;
    }
    s_a2dp_data_diag.last_audio_read_error = err;

    const uint64_t failures = s_a2dp_data_diag.audio_read_failures;
    const bool should_log =
        failures == 1U ||
        (failures % A2DP_DATA_ERROR_LOG_INTERVAL) == 0U;
    if (!should_log) {
        if (s_a2dp_data_diag.suppressed_audio_read_error_logs != UINT32_MAX) {
            s_a2dp_data_diag.suppressed_audio_read_error_logs++;
        }
        return;
    }

    ESP_LOGE(TAG,
             "A2DP data callback audio_processor_read failed: requested=%u error=%s failures=%llu suppressed_logs=%u",
             (unsigned)requested,
             esp_err_to_name(err),
             (unsigned long long)failures,
             (unsigned)s_a2dp_data_diag.suppressed_audio_read_error_logs);
}

""",
)
replace_once(
    a2dp,
    """#ifdef ESP_PLATFORM
// Updated to match esp_a2d_source_data_cb_t: fill buffer and return bytes written
""",
    """#if defined(ESP_PLATFORM) || defined(UNIT_TEST)
// Updated to match esp_a2d_source_data_cb_t: fill buffer and return bytes written
""",
)
replace_once(
    a2dp,
    """    esp_err_t ret = audio_processor_read(buf, req, &bytes_read);
    if (ret != ESP_OK) {
        return 0;
    }

    return (int32_t)bytes_read;
}
#endif // ESP_PLATFORM
""",
    """    esp_err_t ret = audio_processor_read(buf, req, &bytes_read);
    if (ret != ESP_OK) {
        a2dp_data_record_audio_read_failure(ret, req);
        return 0;
    }

    return (int32_t)bytes_read;
}
#endif // ESP_PLATFORM || UNIT_TEST
""",
)
replace_once(
    a2dp,
    """void bt_events_a2dp_test_reset_secondary_errors(void)
{
    s_test_last_generation_diag_update_error = ESP_OK;
    s_test_last_binding_clear_error = ESP_OK;
}
""",
    """void bt_events_a2dp_test_reset_secondary_errors(void)
{
    s_test_last_generation_diag_update_error = ESP_OK;
    s_test_last_binding_clear_error = ESP_OK;
    s_test_last_connection_policy_error = ESP_OK;
}

void bt_events_a2dp_test_reset_telemetry_errors(void)
{
    s_test_last_stale_record_error = ESP_OK;
    s_test_last_unbound_status_error = ESP_OK;
}

void bt_events_a2dp_test_reset_data_diagnostics(void)
{
    memset(&s_a2dp_data_diag, 0, sizeof(s_a2dp_data_diag));
    s_a2dp_data_diag.last_audio_read_error = ESP_OK;
}
""",
)
insert_before_once(
    a2dp,
    "esp_err_t bt_events_a2dp_test_prepare_audio_event(\n",
    """esp_err_t bt_events_a2dp_test_get_last_stale_record_error(void)
{
    return s_test_last_stale_record_error;
}

esp_err_t bt_events_a2dp_test_get_last_unbound_status_error(void)
{
    return s_test_last_unbound_status_error;
}

esp_err_t bt_events_a2dp_test_get_last_connection_policy_error(void)
{
    return s_test_last_connection_policy_error;
}

esp_err_t bt_events_a2dp_test_get_data_diagnostics(
    bt_events_a2dp_data_diag_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    out->audio_read_failures = s_a2dp_data_diag.audio_read_failures;
    out->last_audio_read_error = s_a2dp_data_diag.last_audio_read_error;
    out->suppressed_audio_read_error_logs =
        s_a2dp_data_diag.suppressed_audio_read_error_logs;
    return ESP_OK;
}

""",
)

# Header declarations for test-only diagnostics.
header = "esp_bt_audio_source/components/bt_manager/include/bt_events_a2dp.h"
replace_once(
    header,
    """} bt_events_a2dp_binding_snapshot_t;

esp_err_t bt_events_a2dp_test_get_binding(
""",
    """} bt_events_a2dp_binding_snapshot_t;

typedef struct {
    uint64_t audio_read_failures;
    esp_err_t last_audio_read_error;
    uint32_t suppressed_audio_read_error_logs;
} bt_events_a2dp_data_diag_snapshot_t;

esp_err_t bt_events_a2dp_test_get_binding(
""",
)
replace_once(
    header,
    """void bt_events_a2dp_test_reset_secondary_errors(void);
esp_err_t bt_events_a2dp_test_get_last_generation_diag_update_error(void);
esp_err_t bt_events_a2dp_test_get_last_binding_clear_error(void);
""",
    """void bt_events_a2dp_test_reset_secondary_errors(void);
void bt_events_a2dp_test_reset_telemetry_errors(void);
void bt_events_a2dp_test_reset_data_diagnostics(void);
esp_err_t bt_events_a2dp_test_get_last_generation_diag_update_error(void);
esp_err_t bt_events_a2dp_test_get_last_binding_clear_error(void);
esp_err_t bt_events_a2dp_test_get_last_stale_record_error(void);
esp_err_t bt_events_a2dp_test_get_last_unbound_status_error(void);
esp_err_t bt_events_a2dp_test_get_last_connection_policy_error(void);
esp_err_t bt_events_a2dp_test_get_data_diagnostics(
    bt_events_a2dp_data_diag_snapshot_t *out);
""",
)

# Mock support for exact stale-recorder and audio-reader failures.
mock = "esp_bt_audio_source/test/host_test/mocks/mock_audio_and_btstate.c"
replace_once(
    mock,
    """static unsigned s_stale_operation_records;
static uint32_t s_last_stale_generation;
static char s_last_stale_peer[BT_DUPLEX_MAC_STR_LEN];
""",
    """static unsigned s_stale_operation_records;
static uint32_t s_last_stale_generation;
static char s_last_stale_peer[BT_DUPLEX_MAC_STR_LEN];
static esp_err_t s_stale_operation_record_result = ESP_OK;
static esp_err_t s_audio_processor_read_result = ESP_OK;
static size_t s_audio_processor_read_bytes;
static unsigned s_audio_processor_read_calls;
""",
)
replace_once(
    mock,
    """    s_stale_operation_records = 0U;
    s_last_stale_generation = 0U;
    memset(s_last_stale_peer, 0, sizeof(s_last_stale_peer));
}
""",
    """    s_stale_operation_records = 0U;
    s_last_stale_generation = 0U;
    memset(s_last_stale_peer, 0, sizeof(s_last_stale_peer));
    s_stale_operation_record_result = ESP_OK;
    s_audio_processor_read_result = ESP_OK;
    s_audio_processor_read_bytes = 0U;
    s_audio_processor_read_calls = 0U;
}
""",
)
insert_before_once(
    mock,
    "/* CODE_REVIEW8 Task B: Test helper to force bt_get_streaming_info failure */\n",
    """void bt_manager_test_set_stale_operation_record_result(esp_err_t result) {
    s_stale_operation_record_result = result;
}

esp_err_t bt_manager_test_get_stale_operation_record_result(void) {
    return s_stale_operation_record_result;
}

void bt_manager_test_set_audio_processor_read_result(esp_err_t result,
                                                     size_t bytes_read) {
    s_audio_processor_read_result = result;
    s_audio_processor_read_bytes = bytes_read;
}

unsigned bt_manager_test_get_audio_processor_read_calls(void) {
    return s_audio_processor_read_calls;
}

""",
)
insert_before_once(
    mock,
    "__attribute__((weak)) bt_err_t bt_start_audio(void) {\n",
    """__attribute__((weak)) esp_err_t audio_processor_read(
    uint8_t *buffer, size_t size, size_t *bytes_read) {
    s_audio_processor_read_calls++;
    if (bytes_read == NULL || (buffer == NULL && size != 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    *bytes_read = 0U;
    if (s_audio_processor_read_result != ESP_OK) {
        return s_audio_processor_read_result;
    }
    size_t produced = s_audio_processor_read_bytes;
    if (produced > size) produced = size;
    if (produced != 0U) memset(buffer, 0, produced);
    *bytes_read = produced;
    return ESP_OK;
}

""",
)
replace_once(
    mock,
    """    if (peer_mac != NULL) {
        strncpy(s_last_stale_peer, peer_mac,
                sizeof(s_last_stale_peer) - 1U);
    }
    return ESP_OK;
}
""",
    """    if (peer_mac != NULL) {
        strncpy(s_last_stale_peer, peer_mac,
                sizeof(s_last_stale_peer) - 1U);
    }
    return s_stale_operation_record_result;
}
""",
)

# Extend the exact secondary-failure suite.
test_path = "esp_bt_audio_source/test/host_test/test_a2dp_secondary_failures_exact.c"
replace_once(
    test_path,
    """extern unsigned bt_manager_test_get_hfp_audio_policy_calls(void);
extern int bt_manager_test_get_last_audio_state(void);
""",
    """extern unsigned bt_manager_test_get_hfp_audio_policy_calls(void);
extern unsigned bt_manager_test_get_stale_operation_records(void);
extern void bt_manager_test_set_stale_operation_record_result(esp_err_t result);
extern int bt_manager_test_get_last_audio_state(void);
""",
)
replace_once(
    test_path,
    """static const uint8_t PEER_BDA[6] = {
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
};
static const char *PEER = "aa:bb:cc:dd:ee:ff";
static const esp_a2d_conn_hdl_t HANDLE = 7;
""",
    """static const uint8_t PEER_BDA[6] = {
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
};
static const uint8_t OTHER_PEER_BDA[6] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66
};
static const char *PEER = "aa:bb:cc:dd:ee:ff";
static const esp_a2d_conn_hdl_t HANDLE = 7;
""",
)
insert_before_once(
    test_path,
    "static esp_a2d_cb_param_t audio_param(esp_a2d_audio_state_t state)\n",
    """static esp_a2d_cb_param_t audio_param_for(
    esp_a2d_audio_state_t state,
    const uint8_t bda[6],
    esp_a2d_conn_hdl_t handle)
{
    esp_a2d_cb_param_t param = {0};
    param.audio_stat.state = state;
    memcpy(param.audio_stat.remote_bda, bda, 6U);
    param.audio_stat.conn_hdl = handle;
    return param;
}

""",
)
replace_once(
    test_path,
    """static esp_a2d_cb_param_t audio_param(esp_a2d_audio_state_t state)
{
    esp_a2d_cb_param_t param = {0};
    param.audio_stat.state = state;
    memcpy(param.audio_stat.remote_bda, PEER_BDA, sizeof(PEER_BDA));
    param.audio_stat.conn_hdl = HANDLE;
    return param;
}
""",
    """static esp_a2d_cb_param_t audio_param(esp_a2d_audio_state_t state)
{
    return audio_param_for(state, PEER_BDA, HANDLE);
}
""",
)
replace_once(
    test_path,
    """    bt_events_a2dp_test_reset_secondary_errors();
    establish_binding();
}
""",
    """    bt_events_a2dp_test_reset_secondary_errors();
    bt_events_a2dp_test_reset_telemetry_errors();
    establish_binding();
}
""",
)
insert_before_once(
    test_path,
    "int main(void)\n",
    """static void test_wrong_peer_telemetry_failure_is_visible_without_state_mutation(void)
{
    const bt_events_a2dp_binding_snapshot_t before = binding_snapshot();
    const unsigned policy_before = bt_manager_test_get_hfp_audio_policy_calls();
    const unsigned records_before = bt_manager_test_get_stale_operation_records();
    bt_manager_test_set_stale_operation_record_result(ESP_ERR_NO_MEM);
    bt_events_a2dp_test_reset_telemetry_errors();
    esp_a2d_cb_param_t param = audio_param_for(
        ESP_A2D_AUDIO_STATE_STARTED, OTHER_PEER_BDA, HANDLE);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_events_a2dp_test_prepare_audio_event(&param));
    const bt_events_a2dp_binding_snapshot_t after = binding_snapshot();
    TEST_ASSERT_EQUAL_STRING(before.peer_mac, after.peer_mac);
    TEST_ASSERT_EQUAL_UINT(before.conn_handle, after.conn_handle);
    TEST_ASSERT_EQUAL_UINT32(before.lifecycle_serial, after.lifecycle_serial);
    TEST_ASSERT_EQUAL_UINT32(before.last_duplex_generation,
                             after.last_duplex_generation);
    TEST_ASSERT_EQUAL_UINT64(before.wrong_peer_rejections + 1U,
                             after.wrong_peer_rejections);
    TEST_ASSERT_EQUAL_UINT64(before.stale_handle_rejections,
                             after.stale_handle_rejections);
    TEST_ASSERT_EQUAL_UINT(policy_before,
                           bt_manager_test_get_hfp_audio_policy_calls());
    TEST_ASSERT_EQUAL_UINT(records_before + 1U,
                           bt_manager_test_get_stale_operation_records());
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM,
                      bt_events_a2dp_test_get_last_stale_record_error());
    TEST_ASSERT_FALSE(bt_ctx.audio_playing);
}

static void test_stale_handle_telemetry_failure_is_visible_without_state_mutation(void)
{
    const bt_events_a2dp_binding_snapshot_t before = binding_snapshot();
    const unsigned policy_before = bt_manager_test_get_hfp_audio_policy_calls();
    bt_manager_test_set_stale_operation_record_result(ESP_ERR_TIMEOUT);
    bt_events_a2dp_test_reset_telemetry_errors();
    esp_a2d_cb_param_t param = audio_param_for(
        ESP_A2D_AUDIO_STATE_STARTED, PEER_BDA, HANDLE + 1U);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_events_a2dp_test_prepare_audio_event(&param));
    const bt_events_a2dp_binding_snapshot_t after = binding_snapshot();
    TEST_ASSERT_EQUAL_STRING(before.peer_mac, after.peer_mac);
    TEST_ASSERT_EQUAL_UINT(before.conn_handle, after.conn_handle);
    TEST_ASSERT_EQUAL_UINT32(before.lifecycle_serial, after.lifecycle_serial);
    TEST_ASSERT_EQUAL_UINT32(before.last_duplex_generation,
                             after.last_duplex_generation);
    TEST_ASSERT_EQUAL_UINT64(before.stale_handle_rejections + 1U,
                             after.stale_handle_rejections);
    TEST_ASSERT_EQUAL_UINT64(before.wrong_peer_rejections,
                             after.wrong_peer_rejections);
    TEST_ASSERT_EQUAL_UINT(policy_before,
                           bt_manager_test_get_hfp_audio_policy_calls());
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,
                      bt_events_a2dp_test_get_last_stale_record_error());
    TEST_ASSERT_FALSE(bt_ctx.audio_playing);
}

static void test_unbound_telemetry_failure_preserves_primary_rejection(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_events_a2dp_test_reset_binding());
    bt_manager_test_set_hfp_policy_status(true, PEER, 41U, ESP_OK);
    bt_manager_test_set_stale_operation_record_result(ESP_ERR_NOT_SUPPORTED);
    bt_events_a2dp_test_reset_telemetry_errors();
    const unsigned policy_before = bt_manager_test_get_hfp_audio_policy_calls();
    esp_a2d_cb_param_t param = audio_param(ESP_A2D_AUDIO_STATE_STARTED);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_events_a2dp_test_prepare_audio_event(&param));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED,
                      bt_events_a2dp_test_get_last_stale_record_error());
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_last_unbound_status_error());
    TEST_ASSERT_EQUAL_UINT(policy_before,
                           bt_manager_test_get_hfp_audio_policy_calls());
    TEST_ASSERT_FALSE(bt_ctx.audio_playing);
}

static void test_unbound_status_lookup_failure_is_visible(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_events_a2dp_test_reset_binding());
    bt_manager_test_set_hfp_policy_status(false, NULL, 0U, ESP_ERR_TIMEOUT);
    bt_events_a2dp_test_reset_telemetry_errors();
    esp_a2d_cb_param_t param = audio_param(ESP_A2D_AUDIO_STATE_STARTED);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_events_a2dp_test_prepare_audio_event(&param));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,
                      bt_events_a2dp_test_get_last_unbound_status_error());
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_last_stale_record_error());
    TEST_ASSERT_FALSE(bt_ctx.audio_playing);
}

static void test_disconnect_not_found_policy_with_successful_clear_is_idempotent(void)
{
    bt_manager_test_set_hfp_policy_status(true, PEER, 41U, ESP_OK);
    bt_manager_test_set_hfp_policy_results(ESP_ERR_NOT_FOUND, ESP_OK);
    bt_events_a2dp_test_reset_secondary_errors();
    send_connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);

    const bt_events_a2dp_binding_snapshot_t after = binding_snapshot();
    TEST_ASSERT_FALSE(after.valid);
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_last_binding_clear_error());
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,
                      bt_events_a2dp_test_get_last_connection_policy_error());
}

static void test_disconnect_not_found_policy_allows_clear_error_to_surface(void)
{
    const bt_events_a2dp_binding_snapshot_t before = binding_snapshot();
    bt_manager_test_set_hfp_policy_status(true, PEER, 41U, ESP_OK);
    bt_manager_test_set_hfp_policy_results(ESP_ERR_NOT_FOUND, ESP_OK);
    bt_events_a2dp_test_reset_secondary_errors();
    bt_manager_test_force_ctx_lock_after_successes(2U, ESP_ERR_TIMEOUT);
    send_connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);

    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,
                      bt_events_a2dp_test_get_last_binding_clear_error());
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,
                      bt_events_a2dp_test_get_last_connection_policy_error());
    const bt_events_a2dp_binding_snapshot_t after = binding_snapshot();
    assert_binding_equal(&before, &after);
}

static void test_disconnect_hard_policy_error_is_not_replaced_by_clear_error(void)
{
    const bt_events_a2dp_binding_snapshot_t before = binding_snapshot();
    bt_manager_test_set_hfp_policy_status(true, PEER, 41U, ESP_OK);
    bt_manager_test_set_hfp_policy_results(ESP_ERR_INVALID_STATE, ESP_OK);
    bt_events_a2dp_test_reset_secondary_errors();
    bt_manager_test_force_ctx_lock_after_successes(2U, ESP_ERR_TIMEOUT);
    send_connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);

    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,
                      bt_events_a2dp_test_get_last_binding_clear_error());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_events_a2dp_test_get_last_connection_policy_error());
    const bt_events_a2dp_binding_snapshot_t after = binding_snapshot();
    assert_binding_equal(&before, &after);
}

""",
)
replace_once(
    test_path,
    """    RUN_TEST(test_late_unbound_terminals_never_request_generation_refresh);
    RUN_TEST(test_unbound_start_prepare_path_returns_exact_invalid_state);
    return UNITY_END();
""",
    """    RUN_TEST(test_late_unbound_terminals_never_request_generation_refresh);
    RUN_TEST(test_unbound_start_prepare_path_returns_exact_invalid_state);
    RUN_TEST(test_wrong_peer_telemetry_failure_is_visible_without_state_mutation);
    RUN_TEST(test_stale_handle_telemetry_failure_is_visible_without_state_mutation);
    RUN_TEST(test_unbound_telemetry_failure_preserves_primary_rejection);
    RUN_TEST(test_unbound_status_lookup_failure_is_visible);
    RUN_TEST(test_disconnect_not_found_policy_with_successful_clear_is_idempotent);
    RUN_TEST(test_disconnect_not_found_policy_allows_clear_error_to_surface);
    RUN_TEST(test_disconnect_hard_policy_error_is_not_replaced_by_clear_error);
    return UNITY_END();
""",
)

# New focused data-callback diagnostics test.
data_test = Path(
    "esp_bt_audio_source/test/host_test/test_a2dp_data_callback_diagnostics.c"
)
if data_test.exists():
    raise RuntimeError(f"unexpected pre-existing file: {data_test}")
data_test.write_text(
    r'''#include <string.h>

#include "unity.h"
#include "bt_events_a2dp.h"

extern void bt_manager_test_reset_btstate_mock(void);
extern void bt_manager_test_set_audio_processor_read_result(
    esp_err_t result, size_t bytes_read);
extern unsigned bt_manager_test_get_audio_processor_read_calls(void);

void setUp(void)
{
    bt_manager_test_reset_btstate_mock();
    bt_events_a2dp_test_reset_data_diagnostics();
}

void tearDown(void)
{
}

static bt_events_a2dp_data_diag_snapshot_t data_snapshot(void)
{
    bt_events_a2dp_data_diag_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_events_a2dp_test_get_data_diagnostics(&snapshot));
    return snapshot;
}

static void test_success_returns_bytes_without_failure_diagnostic(void)
{
    uint8_t buffer[32];
    memset(buffer, 0xA5, sizeof(buffer));
    bt_manager_test_set_audio_processor_read_result(ESP_OK, 12U);

    TEST_ASSERT_EQUAL_INT32(12, bt_events_a2dp_data_callback(buffer, 32));
    TEST_ASSERT_EQUAL_UINT(1U,
                           bt_manager_test_get_audio_processor_read_calls());
    const bt_events_a2dp_data_diag_snapshot_t snapshot = data_snapshot();
    TEST_ASSERT_EQUAL_UINT64(0U, snapshot.audio_read_failures);
    TEST_ASSERT_EQUAL(ESP_OK, snapshot.last_audio_read_error);
    TEST_ASSERT_EQUAL_UINT32(0U, snapshot.suppressed_audio_read_error_logs);
}

static void test_failure_returns_zero_and_records_exact_error(void)
{
    uint8_t buffer[32];
    bt_manager_test_set_audio_processor_read_result(ESP_ERR_TIMEOUT, 0U);

    TEST_ASSERT_EQUAL_INT32(0, bt_events_a2dp_data_callback(buffer, 32));
    const bt_events_a2dp_data_diag_snapshot_t snapshot = data_snapshot();
    TEST_ASSERT_EQUAL_UINT64(1U, snapshot.audio_read_failures);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, snapshot.last_audio_read_error);
    TEST_ASSERT_EQUAL_UINT32(0U, snapshot.suppressed_audio_read_error_logs);
}

static void test_repeated_failures_are_counted_with_bounded_logging(void)
{
    uint8_t buffer[8];
    bt_manager_test_set_audio_processor_read_result(ESP_ERR_NOT_SUPPORTED, 0U);

    for (unsigned i = 0U; i < 65U; ++i) {
        TEST_ASSERT_EQUAL_INT32(0,
                                bt_events_a2dp_data_callback(buffer, 8));
    }

    const bt_events_a2dp_data_diag_snapshot_t snapshot = data_snapshot();
    TEST_ASSERT_EQUAL_UINT64(65U, snapshot.audio_read_failures);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, snapshot.last_audio_read_error);
    TEST_ASSERT_EQUAL_UINT32(63U,
                             snapshot.suppressed_audio_read_error_logs);
}

static void test_invalid_requests_do_not_call_audio_processor_or_count_read_failure(void)
{
    uint8_t buffer[8];
    bt_manager_test_set_audio_processor_read_result(ESP_ERR_TIMEOUT, 0U);

    TEST_ASSERT_EQUAL_INT32(0, bt_events_a2dp_data_callback(NULL, 8));
    TEST_ASSERT_EQUAL_INT32(0, bt_events_a2dp_data_callback(buffer, -1));
    TEST_ASSERT_EQUAL_INT32(0, bt_events_a2dp_data_callback(buffer, 0));
    TEST_ASSERT_EQUAL_UINT(0U,
                           bt_manager_test_get_audio_processor_read_calls());
    const bt_events_a2dp_data_diag_snapshot_t snapshot = data_snapshot();
    TEST_ASSERT_EQUAL_UINT64(0U, snapshot.audio_read_failures);
    TEST_ASSERT_EQUAL(ESP_OK, snapshot.last_audio_read_error);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_success_returns_bytes_without_failure_diagnostic);
    RUN_TEST(test_failure_returns_zero_and_records_exact_error);
    RUN_TEST(test_repeated_failures_are_counted_with_bounded_logging);
    RUN_TEST(test_invalid_requests_do_not_call_audio_processor_or_count_read_failure);
    return UNITY_END();
}
''',
    encoding="utf-8",
)

# Register the new test in both focused and complete host graphs.
focused_cmake = (
    "esp_bt_audio_source/test/host_test/a2dp_binding_lifecycle/CMakeLists.txt"
)
insert_before_once(
    focused_cmake,
    "add_a2dp_binding_test(\n    test_bt_manager_init_rollback\n",
    """add_a2dp_binding_test(
    test_a2dp_data_callback_diagnostics
    test_a2dp_data_callback_diagnostics.c
)
target_sources(test_a2dp_data_callback_diagnostics PRIVATE
    "${HOST_TEST_DIR}/mocks/fake_log.c"
)
target_compile_definitions(test_a2dp_data_callback_diagnostics PRIVATE
    CONFIG_BT_MOCK_TESTING=1
)

""",
)
main_cmake = "esp_bt_audio_source/test/host_test/CMakeLists.txt"
insert_before_once(
    main_cmake,
    "add_a2dp_stabilization_test(\n    test_bt_manager_init_rollback\n",
    """add_a2dp_stabilization_test(
    test_a2dp_data_callback_diagnostics
    test_a2dp_data_callback_diagnostics.c
)
add_test(NAME test_a2dp_data_callback_diagnostics
         COMMAND $<TARGET_FILE:test_a2dp_data_callback_diagnostics>)

""",
)
replace_once(
    "esp_bt_audio_source/tools/run_bt_a2dp_binding_lifecycle_test.sh",
    """        test_a2dp_binding_diagnostics_exact \\
        test_a2dp_cross_session_exact \\
        test_a2dp_secondary_failures_exact
""",
    """        test_a2dp_binding_diagnostics_exact \\
        test_a2dp_cross_session_exact \\
        test_a2dp_secondary_failures_exact \\
        test_a2dp_data_callback_diagnostics
""",
)

# FIX-05: make the original TODO's status navigation explicit.
original_todo = (
    "esp_bt_audio_source/docs/"
    "ESP_BT_AUDIO_DUPLEX_STABILIZATION_AND_DEVICE_HANDOFF_TODO_2026-07-29.md"
)
insert_before_once(
    original_todo,
    "---\n\n## 1. Why this TODO exists\n",
    """# Software closeout status

The software-only stabilization work was closed out in:

`esp_bt_audio_source/docs/ESP_BT_AUDIO_DUPLEX_STABILIZATION_SOFTWARE_CLOSEOUT_AND_CLAUDE_DEVICE_HANDOFF_2026-07-29.md`

A later review identified additional software follow-up tasks in:

`esp_bt_audio_source/docs/ESP_BT_AUDIO_DUPLEX_STABILIZATION_REVIEW_FIXES_TODO_2026-07-30.md`

Do not treat unchecked software boxes below as the sole source of current
status without reading those closeout and follow-up files. Physical hardware
tasks remain open until real ESP32-WROOM-32 evidence is captured.

---

""",
)

# Basic postconditions and whitespace checks.
required_paths = [
    ".github/workflows/ci-host-tests.yml",
    a2dp,
    header,
    mock,
    test_path,
    str(data_test),
    focused_cmake,
    main_cmake,
    "esp_bt_audio_source/tools/run_bt_a2dp_binding_lifecycle_test.sh",
    original_todo,
]
for required in required_paths:
    if not Path(required).is_file():
        raise RuntimeError(f"missing required patched file: {required}")

print("Applied duplex stabilization review fixes:")
for required in required_paths:
    print(f"  {required}")
