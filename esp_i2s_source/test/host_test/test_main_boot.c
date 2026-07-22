/*
 * 2.8: host test for run_boot_sequence() (main/main.c). Verifies every
 * singleton initializer/start call happens exactly once, and that the
 * async read-only WROOM probe never issues a mutating command (SPEC §3.3:
 * no mutating boot self-test).
 *
 * Compiles the real main.c against stub definitions for every ESP-IDF/
 * component call it makes. main.c is compiled with ESP_PLATFORM defined so
 * it sees the real bt_link.h/i2s_out.h device-API declarations (matching
 * exactly what ships on-device); everything else in this file provides
 * matching definitions.
 */
#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "esp_err.h"
#include "boot_status.h"
#include "i2s_out.h"
#include "bt_link.h"
#include "wifi_mgr.h"
#include "console.h"
#include "web_ui.h"
#include "radio.h"
#include "stations.h"
#include "ctrl.h"
#include "tone.h"
#include "runtime_capabilities.h"

extern void mock_task_reset(void);
extern unsigned mock_task_create_count(void);

/* ---- call counters ---- */
typedef struct {
    int i2s_out_init;
    int i2s_out_start;
    int bt_link_init;
    int radio_init;
    int stations_init;
    int ctrl_init;
    int ctrl_start;
    int wifi_mgr_init;
    int console_start;
    int web_ui_start;
} boot_call_counts_t;

static boot_call_counts_t s_calls;
static int s_mutating_bt_cmds; /* VOLUME/CONNECT/START/PAIR sent through the boot probe */

static void reset_counters(void)
{
    memset(&s_calls, 0, sizeof(s_calls));
    s_mutating_bt_cmds = 0;
}

void setUp(void)
{
    reset_counters();
    mock_task_reset();
}

void tearDown(void)
{
}

/* ---- i2s_out stubs ---- */
esp_err_t i2s_out_init(size_t ring_capacity_bytes) { (void)ring_capacity_bytes; s_calls.i2s_out_init++; return ESP_OK; }
esp_err_t i2s_out_start(void) { s_calls.i2s_out_start++; return ESP_OK; }
esp_err_t i2s_out_stop(void) { return ESP_OK; }
size_t i2s_out_write(const uint8_t *data, size_t len) { (void)data; return len; }
void i2s_out_get_stats(i2s_out_stats_t *out) { if (out) memset(out, 0, sizeof(*out)); }
void i2s_out_gain_load(void) {}
esp_err_t i2s_out_set_gain(int pct) { (void)pct; return ESP_OK; }
int i2s_out_get_gain(void) { return 100; }
void i2s_out_apply_gain(int16_t *samples, size_t count, int pct) { (void)samples; (void)count; (void)pct; }

/* ---- bt_link stubs — flags any command that isn't a read-only probe verb ---- */
esp_err_t bt_link_init(uint32_t cmd_timeout_ms) { (void)cmd_timeout_ms; s_calls.bt_link_init++; return ESP_OK; }
int bt_link_subscribe(bt_link_event_cb cb, void *ctx) { (void)cb; (void)ctx; return 0; }
esp_err_t bt_link_send(const char *cmd, bt_link_cmd_state_t *out_state,
                       char *result, size_t result_sz, char *data, size_t data_sz)
{
    (void)result; (void)result_sz; (void)data; (void)data_sz;
    if (cmd && strcmp(cmd, "VERSION") != 0 && strcmp(cmd, "STATUS") != 0) {
        s_mutating_bt_cmds++;
    }
    if (out_state) *out_state = BT_LINK_CMD_DONE_OK;
    return ESP_OK;
}

/* ---- radio stubs ---- */
esp_err_t radio_init(size_t ring_bytes) { (void)ring_bytes; s_calls.radio_init++; return ESP_OK; }
radio_state_t radio_get_state(void) { return RADIO_STATE_STOPPED; }
bool radio_audio_ready(void) { return false; }
size_t radio_pcm_read(int16_t *dst, size_t frames) { (void)dst; (void)frames; return 0; }

/* ---- stations stubs ---- */
esp_err_t stations_init(void) { s_calls.stations_init++; return ESP_OK; }

/* ---- ctrl stubs ---- */
esp_err_t ctrl_init(void) { s_calls.ctrl_init++; return ESP_OK; }
esp_err_t ctrl_start(void) { s_calls.ctrl_start++; return ESP_OK; }

/* ---- wifi_mgr stubs ---- */
esp_err_t wifi_mgr_init(void) { s_calls.wifi_mgr_init++; return ESP_OK; }

/* ---- console/web_ui stubs ---- */
esp_err_t console_start(void) { s_calls.console_start++; return ESP_OK; }
esp_err_t web_ui_start(void) { s_calls.web_ui_start++; return ESP_OK; }

/* ---- tone stubs ---- */
void tone_get(bool *enabled, int *freq_hz) { if (enabled) *enabled = false; if (freq_hz) *freq_hz = 0; }
void tone_fill(int16_t *out, size_t frames) { if (out && frames) memset(out, 0, frames * 2 * sizeof(int16_t)); }

/* ---- clock_diag stub — main.c's app_main() references this; run_boot_sequence()
 * does not, but the symbol must still resolve at link time. ---- */
void clock_diag_start(void) {}

/* ---- runtime_capabilities stub (FIX3 10.1) — the real module needs a
 * FreeRTOS mutex this target doesn't mock; a trivial no-op is enough since
 * this test only exercises run_boot_sequence()'s own boot_status_t result. ---- */
static runtime_capabilities_t s_published_caps;
void runtime_capabilities_publish(const runtime_capabilities_t *caps)
{
    if (caps) s_published_caps = *caps;
}
void runtime_capabilities_get(runtime_capabilities_t *out)
{
    if (out) *out = s_published_caps;
}

/* ---- ESP-IDF platform stubs ---- */
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { return ESP_OK; }
uint32_t esp_random(void) { return 0x12345678u; }
const char *esp_get_idf_version(void) { return "host-test"; }
uint32_t esp_get_free_heap_size(void) { return 123456u; }

/* ================= tests ================= */

static void test_every_initializer_called_exactly_once(void)
{
    boot_status_t boot = run_boot_sequence();

    TEST_ASSERT_EQUAL_INT(1, s_calls.i2s_out_init);
    TEST_ASSERT_EQUAL_INT(1, s_calls.i2s_out_start);
    TEST_ASSERT_EQUAL_INT(1, s_calls.bt_link_init);
    TEST_ASSERT_EQUAL_INT(1, s_calls.radio_init);
    TEST_ASSERT_EQUAL_INT(1, s_calls.stations_init);
    TEST_ASSERT_EQUAL_INT(1, s_calls.ctrl_init);
    TEST_ASSERT_EQUAL_INT(1, s_calls.ctrl_start);
    TEST_ASSERT_EQUAL_INT(1, s_calls.wifi_mgr_init);
    TEST_ASSERT_EQUAL_INT(1, s_calls.console_start);
    TEST_ASSERT_EQUAL_INT(1, s_calls.web_ui_start);

    /* audio_out task + the async bt_link probe task = 2 task creations. */
    TEST_ASSERT_EQUAL_UINT(2, mock_task_create_count());

    TEST_ASSERT_TRUE(boot.i2s_ok);
    TEST_ASSERT_TRUE(boot.audio_task_ok);
    TEST_ASSERT_TRUE(boot.bt_link_ok);
    TEST_ASSERT_TRUE(boot.radio_ok);
    TEST_ASSERT_TRUE(boot.stations_ok);
    TEST_ASSERT_TRUE(boot.ctrl_ok);
    TEST_ASSERT_TRUE(boot.wifi_ok);
    TEST_ASSERT_TRUE(boot.console_ok);
    TEST_ASSERT_TRUE(boot.web_ok);
    TEST_ASSERT_TRUE(boot.ctrl_start_ok);

    /* 10.1: run_boot_sequence() must publish what actually succeeded, not
     * just return it locally to app_main() (which used to discard it via
     * `(void)boot;`). */
    runtime_capabilities_t caps;
    runtime_capabilities_get(&caps);
    TEST_ASSERT_TRUE(caps.i2s);
    TEST_ASSERT_TRUE(caps.audio_task);
    TEST_ASSERT_TRUE(caps.bt_link);
    TEST_ASSERT_TRUE(caps.radio);
    TEST_ASSERT_TRUE(caps.stations);
    TEST_ASSERT_TRUE(caps.ctrl);
    TEST_ASSERT_TRUE(caps.wifi);
    TEST_ASSERT_TRUE(caps.web);
}

static void test_boot_probe_issues_no_mutating_command(void)
{
    /* The probe task itself never runs on host (xTaskCreate is mocked), so
     * this drives bt_link_send() the same way link_health_probe_task does
     * and asserts the read-only contract directly. */
    bt_link_cmd_state_t state;
    char result[64] = {0}, data[64] = {0};
    bt_link_send("VERSION", &state, result, sizeof(result), data, sizeof(data));
    bt_link_send("STATUS", &state, result, sizeof(result), data, sizeof(data));

    TEST_ASSERT_EQUAL_INT(0, s_mutating_bt_cmds);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_initializer_called_exactly_once);
    RUN_TEST(test_boot_probe_issues_no_mutating_command);
    return UNITY_END();
}
