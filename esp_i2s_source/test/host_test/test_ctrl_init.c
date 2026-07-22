/* RH-S3-10: host tests for ctrl_init() startup ordering.
 *
 * Verifies that ctrl_init() creates the mutex before ctrl_start(), and that
 * public APIs return ESP_ERR_INVALID_STATE (or no-op) when called before init.
 */
#include "unity.h"
#include "esp_err.h"
#include "ctrl.h"
#include "ctrl_cfg.h"
#include "freertos/FreeRTOS.h"

#include <string.h>
#include <stdlib.h>

/* Mock control functions */
extern void mock_task_reset(void);
extern void mock_sem_reset(void);
extern void mock_task_set_create_result(int result);
extern void mock_ctrl_cfg_set_save_err(esp_err_t err);
extern void mock_ctrl_cfg_set_legacy(bool needs_resolve, int16_t legacy_index);
extern void mock_ctrl_cfg_reset(void);
extern void mock_stations_set_resolve_legacy_result(esp_err_t err, uint32_t station_id);

static void reset_ctrl_state(void)
{
    mock_task_reset();
    mock_sem_reset();
    mock_ctrl_cfg_set_save_err(ESP_OK);
}

void setUp(void)
{
    reset_ctrl_state();
}

void tearDown(void)
{
}

/* --- before-init tests (must run before any test calls ctrl_init()) --- */

static void test_ctrl_set_sink_before_init_fails(void)
{
    /* ctrl_set_sink() should return ESP_ERR_INVALID_STATE before init. */
    esp_err_t err = ctrl_set_sink("A0:B7:65:2B:E6:5E", true, 50);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
}

static void test_ctrl_scan_before_init_fails(void)
{
    /* ctrl_scan() should return ESP_ERR_INVALID_STATE before init. */
    esp_err_t err = ctrl_scan();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
}

static void test_ctrl_get_cfg_before_init_noop(void)
{
    /* ctrl_get_cfg() should be a safe no-op before init (does not crash). */
    ctrl_cfg_t cfg;
    /* Uninitialized memory — the call should not crash or access invalid pointers. */
    ctrl_get_cfg(&cfg);
    /* Defensive behavior: returns without modifying out. Test just verifies no crash. */
}

static void test_ctrl_note_station_before_init_noop(void)
{
    /* ctrl_note_station() should return ESP_ERR_INVALID_STATE before init. */
    esp_err_t err = ctrl_note_station(42);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
}

static void test_ctrl_scan_active_before_init(void)
{
    /* ctrl_scan_active() should return false before init. */
    bool active = ctrl_scan_active();
    TEST_ASSERT_FALSE(active);
}

/* --- FIX3 9.4: coordinator-design legacy migration ---
 * Must run as the very FIRST call to ctrl_init() in this binary (ctrl_init()
 * is idempotent — every call after the first is a no-op that doesn't
 * re-load). Resolves to CTRL_LAST_STATION_NONE (the "station not found"
 * branch) so it doesn't disturb the after-init tests below, which assume a
 * fresh/default config. The index-0->ID-1 mapping itself is already
 * covered exhaustively by test_stations_persistence.c (Phase 5A); this
 * verifies ctrl_init() actually wires stations_resolve_legacy_index() in as
 * the coordinator, including the failure path. */
static void test_ctrl_init_resolves_legacy_station_not_found(void)
{
    mock_ctrl_cfg_set_legacy(true, 3);
    mock_stations_set_resolve_legacy_result(ESP_ERR_NOT_FOUND, 0);

    esp_err_t err = ctrl_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    ctrl_cfg_t cfg;
    ctrl_get_cfg(&cfg);
    TEST_ASSERT_EQUAL_UINT32(CTRL_LAST_STATION_NONE, cfg.last_station_id);

    /* Don't let this leak into later tests calling ctrl_init() again
     * (harmless no-op since it's idempotent, but keep the mock state sane). */
    mock_ctrl_cfg_set_legacy(false, CTRL_STATION_NONE);
    mock_stations_set_resolve_legacy_result(ESP_ERR_NOT_FOUND, 0);
}

/* --- after-init tests --- */

static void test_ctrl_init_creates_mutex(void)
{
    /* ctrl_init() should succeed and create the mutex. */
    esp_err_t err = ctrl_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Subsequent calls should be idempotent. */
    err = ctrl_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

static void test_ctrl_get_cfg_after_init(void)
{
    /* After ctrl_init(), ctrl_get_cfg() should return the loaded config. */
    esp_err_t err = ctrl_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    ctrl_cfg_t cfg;
    ctrl_get_cfg(&cfg);
    /* Defaults from the host ctrl_cfg_load() stub: no sink, autostart off,
     * no station to resume, conservative default volume. */
    TEST_ASSERT_EQUAL_STRING("", cfg.sink_mac);
    TEST_ASSERT_EQUAL(0, cfg.autostart);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.last_station_id);
    TEST_ASSERT_EQUAL(CTRL_VOLUME_DEFAULT, cfg.volume);
}

static void test_ctrl_set_sink_after_init(void)
{
    /* After ctrl_init(), ctrl_set_sink() should work. */
    esp_err_t err = ctrl_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = ctrl_set_sink("A0:B7:65:2B:E6:5E", true, 50);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Verify the config was updated. */
    ctrl_cfg_t cfg;
    ctrl_get_cfg(&cfg);
    TEST_ASSERT_EQUAL_STRING("A0:B7:65:2B:E6:5E", cfg.sink_mac);
    TEST_ASSERT_EQUAL(1, cfg.autostart);
    TEST_ASSERT_EQUAL(50, cfg.volume);
}

static void test_ctrl_note_station_after_init(void)
{
    /* After ctrl_init(), ctrl_note_station() should work without crashing. */
    esp_err_t err = ctrl_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = ctrl_note_station(7);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

/* FIX3 9.3: a persistence failure must leave s_cfg exactly as it was —
 * never publish a candidate before ctrl_cfg_save() succeeds. */
static void test_ctrl_set_sink_persistence_failure_leaves_cfg_unchanged(void)
{
    esp_err_t err = ctrl_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Establish a known-good baseline. */
    err = ctrl_set_sink("11:22:33:44:55:66", false, 20);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    ctrl_cfg_t before;
    ctrl_get_cfg(&before);

    /* Now inject a persistence failure and attempt a different update. */
    mock_ctrl_cfg_set_save_err(ESP_ERR_NVS_NOT_FOUND);
    err = ctrl_set_sink("AA:BB:CC:DD:EE:FF", true, 99);
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, err);

    ctrl_cfg_t after;
    ctrl_get_cfg(&after);
    TEST_ASSERT_EQUAL_STRING(before.sink_mac, after.sink_mac);
    TEST_ASSERT_EQUAL(before.autostart, after.autostart);
    TEST_ASSERT_EQUAL(before.volume, after.volume);

    mock_ctrl_cfg_set_save_err(ESP_OK);
}

/* FIX3 9.2: ctrl_start() must never overwrite a live task handle. */
static void test_ctrl_start_rejects_duplicate_task(void)
{
    esp_err_t err = ctrl_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    mock_task_set_create_result(pdPASS);
    err = ctrl_start();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* The mocked task body never runs (no real concurrency in host tests),
     * so s_task is still the live handle from the call above -- a second
     * ctrl_start() must be rejected, not spawn a second orchestrator. */
    err = ctrl_start();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
}

int main(void)
{
    UNITY_BEGIN();
    /* Before-init tests — these verify defensive behavior. */
    RUN_TEST(test_ctrl_set_sink_before_init_fails);
    RUN_TEST(test_ctrl_scan_before_init_fails);
    RUN_TEST(test_ctrl_get_cfg_before_init_noop);
    RUN_TEST(test_ctrl_note_station_before_init_noop);
    RUN_TEST(test_ctrl_scan_active_before_init);
    /* FIX3 9.4: must be the very first call to ctrl_init() in this binary. */
    RUN_TEST(test_ctrl_init_resolves_legacy_station_not_found);
    /* After-init tests — these verify correct operation. */
    RUN_TEST(test_ctrl_init_creates_mutex);
    RUN_TEST(test_ctrl_get_cfg_after_init);
    RUN_TEST(test_ctrl_set_sink_after_init);
    RUN_TEST(test_ctrl_note_station_after_init);
    RUN_TEST(test_ctrl_set_sink_persistence_failure_leaves_cfg_unchanged);
    RUN_TEST(test_ctrl_start_rejects_duplicate_task);
    return UNITY_END();
}
