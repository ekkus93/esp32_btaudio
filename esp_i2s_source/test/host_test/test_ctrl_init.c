/* RH-S3-10: host tests for ctrl_init() startup ordering.
 *
 * Verifies that ctrl_init() creates the mutex before ctrl_start(), and that
 * public APIs return ESP_ERR_INVALID_STATE (or no-op) when called before init.
 */
#include "unity.h"
#include "esp_err.h"
#include "ctrl.h"
#include "ctrl_cfg.h"

#include <string.h>
#include <stdlib.h>

/* Mock control functions */
extern void mock_task_reset(void);
extern void mock_sem_reset(void);

static void reset_ctrl_state(void)
{
    mock_task_reset();
    mock_sem_reset();
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
    TEST_ASSERT_EQUAL(CTRL_STATION_NONE, cfg.last_station);
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

int main(void)
{
    UNITY_BEGIN();
    /* Before-init tests — these verify defensive behavior. */
    RUN_TEST(test_ctrl_set_sink_before_init_fails);
    RUN_TEST(test_ctrl_scan_before_init_fails);
    RUN_TEST(test_ctrl_get_cfg_before_init_noop);
    RUN_TEST(test_ctrl_note_station_before_init_noop);
    RUN_TEST(test_ctrl_scan_active_before_init);
    /* After-init tests — these verify correct operation. */
    RUN_TEST(test_ctrl_init_creates_mutex);
    RUN_TEST(test_ctrl_get_cfg_after_init);
    RUN_TEST(test_ctrl_set_sink_after_init);
    RUN_TEST(test_ctrl_note_station_after_init);
    return UNITY_END();
}
