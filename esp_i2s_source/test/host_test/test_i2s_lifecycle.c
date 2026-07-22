/*
 * test_i2s_lifecycle — FIX3 Phase 3 (TODO 3.7): i2s_out.c lifecycle state
 * machine — join-safety, truthful state publication, channel-enabled
 * tracking.
 *
 * The shared mocks/fake_task.c xTaskCreate() never actually runs the
 * created task (host tests have no real concurrency), so a real
 * writer_task() never executes here. Instead:
 *   - A local xTaskCreate() simulates the writer's very first action
 *     (what it would publish immediately after being scheduled) via
 *     i2s_test_inject_writer_state()/i2s_test_inject_writer_bits().
 *   - A local event-group mock supports a small queue of programmed
 *     xEventGroupWaitBits() return values, so a test can give two
 *     sequential waits (start()'s READY|EXITED wait, then
 *     join_writer_locked()'s EXITED wait during cancellation) two
 *     different, independently-controlled outcomes — something a plain
 *     current-bits mock can't express without real concurrency.
 */
#include "unity.h"
#include "i2s_out.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "driver/i2s_std.h"
#include "nvs.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ================= Local FreeRTOS event-group mock ================= */

typedef struct {
    EventBits_t bits;
} mock_eg_t;

#define WAIT_QUEUE_MAX 4
static EventBits_t s_wait_queue[WAIT_QUEUE_MAX];
static int         s_wait_queue_len;

/* Schedule the return value of the next xEventGroupWaitBits() call. Calls
 * beyond the queued count fall back to real current-bits behavior. */
static void mock_evt_queue_wait_result(EventBits_t bits)
{
    TEST_ASSERT_LESS_THAN(WAIT_QUEUE_MAX, s_wait_queue_len);
    s_wait_queue[s_wait_queue_len++] = bits;
}

static void mock_evt_queue_reset(void)
{
    s_wait_queue_len = 0;
    memset(s_wait_queue, 0, sizeof(s_wait_queue));
}

EventGroupHandle_t xEventGroupCreate(void)
{
    mock_eg_t *e = (mock_eg_t *)calloc(1, sizeof(*e));
    return (EventGroupHandle_t)e;
}

void vEventGroupDelete(EventGroupHandle_t eg)
{
    free(eg);
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t eg, EventBits_t bits)
{
    if (!eg) return 0;
    mock_eg_t *e = (mock_eg_t *)eg;
    EventBits_t prev = e->bits;
    e->bits |= bits;
    return prev;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t eg, EventBits_t bits)
{
    if (!eg) return 0;
    mock_eg_t *e = (mock_eg_t *)eg;
    EventBits_t prev = e->bits;
    e->bits &= ~bits;
    return prev;
}

EventBits_t xEventGroupGetBits(EventGroupHandle_t eg)
{
    if (!eg) return 0;
    return ((mock_eg_t *)eg)->bits;
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t eg, EventBits_t bits_to_wait,
                                BaseType_t clear_on_exit, BaseType_t wait_for_all,
                                TickType_t ticks)
{
    (void)bits_to_wait;
    (void)wait_for_all;
    (void)ticks;
    if (s_wait_queue_len > 0) {
        EventBits_t result = s_wait_queue[0];
        for (int i = 1; i < s_wait_queue_len; i++) s_wait_queue[i - 1] = s_wait_queue[i];
        s_wait_queue_len--;
        if (eg && clear_on_exit) {
            ((mock_eg_t *)eg)->bits &= ~result;
        }
        return result;
    }
    if (!eg) return 0;
    mock_eg_t *e = (mock_eg_t *)eg;
    EventBits_t current = e->bits;
    if (clear_on_exit) e->bits = 0;
    return current;
}

/* ================= Local FreeRTOS task mock ================= */

/* What the "writer" should simulate publishing the instant xTaskCreate()
 * succeeds — proxy for "the real writer task ran almost immediately." */
typedef enum {
    SIM_RUNNING,       /* publish RUNNING + READY */
    SIM_WAITING,       /* publish WAITING_FOR_CLOCK + READY */
    SIM_FAULTED,       /* publish FAULTED + READY (+ EXITED, matches real code) */
    SIM_SILENT,        /* publish nothing — simulate a hung/slow writer */
} sim_writer_t;

static sim_writer_t s_sim_writer = SIM_RUNNING;
static esp_err_t    s_sim_fault_err = ESP_FAIL;
static BaseType_t   s_task_create_result = pdPASS;
static unsigned     s_task_create_count;
static unsigned     s_task_delete_count;

static void reset_task_mock(void)
{
    s_sim_writer = SIM_RUNNING;
    s_sim_fault_err = ESP_FAIL;
    s_task_create_result = pdPASS;
    s_task_create_count = 0;
    s_task_delete_count = 0;
}

BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                       void *param, unsigned prio, TaskHandle_t *out_handle)
{
    (void)task; (void)name; (void)stack; (void)param; (void)prio;
    s_task_create_count++;
    if (s_task_create_result != pdPASS) {
        if (out_handle) *out_handle = NULL;
        return s_task_create_result;
    }
    if (out_handle) *out_handle = (TaskHandle_t)(uintptr_t)0x1;

    switch (s_sim_writer) {
    case SIM_RUNNING:
        i2s_test_inject_writer_state(I2S_STATE_RUNNING, ESP_OK);
        i2s_test_inject_writer_bits(I2S_TEST_EVT_WRITER_READY);
        break;
    case SIM_WAITING:
        i2s_test_inject_writer_state(I2S_STATE_WAITING_FOR_CLOCK, ESP_OK);
        i2s_test_inject_writer_bits(I2S_TEST_EVT_WRITER_READY);
        break;
    case SIM_FAULTED:
        i2s_test_inject_writer_state(I2S_STATE_FAULTED, s_sim_fault_err);
        i2s_test_inject_writer_bits(I2S_TEST_EVT_WRITER_READY | I2S_TEST_EVT_WRITER_EXITED);
        break;
    case SIM_SILENT:
    default:
        break; /* nothing published — simulate a hung/slow writer */
    }
    return pdPASS;
}

void vTaskDelete(TaskHandle_t task)
{
    (void)task;
    s_task_delete_count++;
}

/* ================= I2S driver mock ================= */

static esp_err_t s_new_channel_result = ESP_OK;
static esp_err_t s_init_std_result = ESP_OK;
static esp_err_t s_enable_result = ESP_OK;
static esp_err_t s_disable_result = ESP_OK;
static esp_err_t s_del_channel_result = ESP_OK;
static unsigned  s_enable_count, s_disable_count, s_del_channel_count;

static void reset_i2s_driver_mock(void)
{
    s_new_channel_result = ESP_OK;
    s_init_std_result = ESP_OK;
    s_enable_result = ESP_OK;
    s_disable_result = ESP_OK;
    s_del_channel_result = ESP_OK;
    s_enable_count = s_disable_count = s_del_channel_count = 0;
}

esp_err_t i2s_new_channel(const i2s_chan_config_t *chan_cfg, i2s_chan_handle_t *tx,
                         i2s_chan_handle_t *rx)
{
    (void)chan_cfg;
    if (rx) *rx = NULL;
    if (s_new_channel_result != ESP_OK) {
        if (tx) *tx = NULL;
        return s_new_channel_result;
    }
    if (tx) *tx = (i2s_chan_handle_t)(uintptr_t)0x100;
    return ESP_OK;
}

esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t handle, const i2s_std_config_t *cfg)
{
    (void)handle; (void)cfg;
    return s_init_std_result;
}

esp_err_t i2s_channel_enable(i2s_chan_handle_t handle)
{
    (void)handle;
    s_enable_count++;
    return s_enable_result;
}

esp_err_t i2s_channel_disable(i2s_chan_handle_t handle)
{
    (void)handle;
    s_disable_count++;
    return s_disable_result;
}

/* Scripted write results for driving writer_step() directly (the task loop
 * itself never runs under the mocked scheduler). Each queued entry supplies
 * (err, written); once drained, writes succeed in full. The mock also
 * captures the timeout argument VERBATIM — i2s_channel_write() takes
 * MILLISECONDS, and this target compiles with configTICK_RATE_HZ=100 so a
 * reintroduced pdMS_TO_TICKS() double-conversion shows up as 10 instead of
 * 100 (the exact 2026-07-22 DMA-starvation bug). */
#define WRITE_SCRIPT_MAX 8
static struct { esp_err_t err; size_t written; } s_write_script[WRITE_SCRIPT_MAX];
static int s_write_script_len;
static int s_write_script_pos;
static uint32_t s_write_last_timeout_arg;

static void mock_write_reset(void)
{
    s_write_script_len = s_write_script_pos = 0;
    s_write_last_timeout_arg = 0;
}

static void mock_write_queue(esp_err_t err, size_t written)
{
    TEST_ASSERT_LESS_THAN(WRITE_SCRIPT_MAX, s_write_script_len);
    s_write_script[s_write_script_len].err = err;
    s_write_script[s_write_script_len].written = written;
    s_write_script_len++;
}

esp_err_t i2s_channel_write(i2s_chan_handle_t handle, const void *src, size_t size,
                           size_t *bytes_written, TickType_t timeout)
{
    (void)handle; (void)src;
    s_write_last_timeout_arg = (uint32_t)timeout;
    if (s_write_script_pos < s_write_script_len) {
        int idx = s_write_script_pos++;
        size_t w = s_write_script[idx].written;
        if (w > size) w = size;
        if (bytes_written) *bytes_written = w;
        return s_write_script[idx].err;
    }
    if (bytes_written) *bytes_written = size;
    return ESP_OK;
}

esp_err_t i2s_del_channel(i2s_chan_handle_t handle)
{
    (void)handle;
    s_del_channel_count++;
    return s_del_channel_result;
}

/* ================= heap_caps + NVS mocks ================= */

void *esp_heap_caps_malloc(size_t size, int caps)
{
    (void)caps;
    return malloc(size);
}

void esp_heap_caps_free(void *ptr)
{
    free(ptr);
}

esp_err_t nvs_open(const char *ns, int flags, nvs_handle_t *out)
{
    (void)ns; (void)flags;
    if (out) *out = 1;
    return ESP_ERR_NVS_NOT_FOUND; /* gain_load(): no persisted value, keep default */
}

esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *out)
{
    (void)h; (void)key; (void)out;
    return ESP_ERR_NVS_NOT_FOUND;
}

void nvs_close(nvs_handle_t h)
{
    (void)h;
}

esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t val)
{
    (void)h; (void)key; (void)val;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    return ESP_OK;
}

/* ================= Test scaffolding ================= */

#define RING_CAP (16 * 1024)

void setUp(void)
{
    mock_evt_queue_reset();
    reset_task_mock();
    reset_i2s_driver_mock();
    mock_write_reset();
}

void tearDown(void)
{
    i2s_test_reset_module_state();
}

static void init_idle(void)
{
    esp_err_t err = i2s_out_init(RING_CAP);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(I2S_STATE_IDLE, i2s_out_get_state());
}

/* ---- TODO 3.7 required proofs ---- */

void test_writer_waiting_before_start_returns_and_state_stays_waiting(void)
{
    init_idle();
    s_sim_writer = SIM_WAITING;

    esp_err_t err = i2s_out_start();

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(I2S_STATE_WAITING_FOR_CLOCK, i2s_out_get_state());
}

void test_writer_faults_before_start_returns_and_state_stays_faulted(void)
{
    init_idle();
    s_sim_writer = SIM_FAULTED;
    s_sim_fault_err = ESP_ERR_INVALID_STATE;

    esp_err_t err = i2s_out_start();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
    TEST_ASSERT_EQUAL(I2S_STATE_FAULTED, i2s_out_get_state());

    i2s_out_stats_t stats;
    i2s_out_get_stats(&stats);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, stats.last_error);
}

void test_start_ack_timeout_cancel_succeeds_reaches_idle(void)
{
    init_idle();
    s_sim_writer = SIM_SILENT;   /* writer never publishes anything */

    /* First wait (READY|EXITED): nothing published -> 0.
     * join_writer_locked()'s wait (EXITED only): simulate the writer
     * exiting promptly once stop_requested is observed. */
    mock_evt_queue_wait_result(0);
    mock_evt_queue_wait_result(I2S_TEST_EVT_WRITER_EXITED);

    esp_err_t err = i2s_out_start();

    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);
    TEST_ASSERT_EQUAL(I2S_STATE_IDLE, i2s_out_get_state());
    TEST_ASSERT_EQUAL(1, s_disable_count);   /* channel was enabled, then disabled */
}

void test_start_ack_timeout_no_exit_becomes_join_pending(void)
{
    init_idle();
    s_sim_writer = SIM_SILENT;

    /* Neither wait ever sees the writer respond. */
    mock_evt_queue_wait_result(0);
    mock_evt_queue_wait_result(0);

    esp_err_t err = i2s_out_start();

    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);
    TEST_ASSERT_EQUAL(I2S_STATE_FAULTED_JOIN_PENDING, i2s_out_get_state());
    /* Retained: disable() must NOT have been called while ownership is
     * unresolved (spec §4.1 — a timeout is not permission to reclaim). */
    TEST_ASSERT_EQUAL(0, s_disable_count);
}

void test_stop_timeout_retains_resources_and_blocks_deinit(void)
{
    init_idle();
    s_sim_writer = SIM_RUNNING;
    TEST_ASSERT_EQUAL(ESP_OK, i2s_out_start());
    TEST_ASSERT_EQUAL(I2S_STATE_RUNNING, i2s_out_get_state());

    /* stop()'s join wait never sees EXITED. */
    mock_evt_queue_wait_result(0);

    esp_err_t err = i2s_out_stop();
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);
    TEST_ASSERT_EQUAL(I2S_STATE_FAULTED_JOIN_PENDING, i2s_out_get_state());
    TEST_ASSERT_EQUAL(0, s_disable_count);

    /* deinit() must refuse while JOIN_PENDING — the writer may still touch
     * s_ring/s_tx_chan/s_events. */
    err = i2s_out_deinit();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
    TEST_ASSERT_EQUAL(0, s_del_channel_count);
}

void test_disable_failure_leaves_faulted_channel_still_enabled(void)
{
    init_idle();
    s_sim_writer = SIM_RUNNING;
    TEST_ASSERT_EQUAL(ESP_OK, i2s_out_start());

    mock_evt_queue_wait_result(I2S_TEST_EVT_WRITER_EXITED); /* join succeeds */
    s_disable_result = ESP_ERR_INVALID_STATE;

    esp_err_t err = i2s_out_stop();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
    TEST_ASSERT_EQUAL(I2S_STATE_FAULTED, i2s_out_get_state());

    /* Deinit must still refuse: channel_enabled is still true. */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, i2s_out_deinit());
}

void test_repeated_stop_retries_disable_and_reaches_idle(void)
{
    init_idle();
    s_sim_writer = SIM_RUNNING;
    TEST_ASSERT_EQUAL(ESP_OK, i2s_out_start());

    mock_evt_queue_wait_result(I2S_TEST_EVT_WRITER_EXITED);
    s_disable_result = ESP_ERR_INVALID_STATE;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, i2s_out_stop());
    TEST_ASSERT_EQUAL(I2S_STATE_FAULTED, i2s_out_get_state());

    /* Retry: join is a no-op this time (s_writer_task already cleared),
     * disable succeeds -> IDLE. */
    s_disable_result = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, i2s_out_stop());
    TEST_ASSERT_EQUAL(I2S_STATE_IDLE, i2s_out_get_state());
    TEST_ASSERT_EQUAL(2, s_disable_count);
}

void test_deinit_rejects_every_non_idle_state(void)
{
    init_idle();

    /* STARTING: mid-flight, writer silent (never reaches an observed state). */
    s_sim_writer = SIM_SILENT;
    mock_evt_queue_wait_result(0); /* first wait in start(): timeout */
    mock_evt_queue_wait_result(0); /* join_writer_locked(): no exit -> JOIN_PENDING */
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, i2s_out_start());
    TEST_ASSERT_EQUAL(I2S_STATE_FAULTED_JOIN_PENDING, i2s_out_get_state());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, i2s_out_deinit());

    /* Recover: writer "exits" on the next stop() retry, disable succeeds. */
    mock_evt_queue_wait_result(I2S_TEST_EVT_WRITER_EXITED);
    TEST_ASSERT_EQUAL(ESP_OK, i2s_out_stop());
    TEST_ASSERT_EQUAL(I2S_STATE_IDLE, i2s_out_get_state());

    /* RUNNING */
    s_sim_writer = SIM_RUNNING;
    TEST_ASSERT_EQUAL(ESP_OK, i2s_out_start());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, i2s_out_deinit());
    mock_evt_queue_wait_result(I2S_TEST_EVT_WRITER_EXITED);
    TEST_ASSERT_EQUAL(ESP_OK, i2s_out_stop());

    /* WAITING_FOR_CLOCK */
    s_sim_writer = SIM_WAITING;
    TEST_ASSERT_EQUAL(ESP_OK, i2s_out_start());
    TEST_ASSERT_EQUAL(I2S_STATE_WAITING_FOR_CLOCK, i2s_out_get_state());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, i2s_out_deinit());
    mock_evt_queue_wait_result(I2S_TEST_EVT_WRITER_EXITED);
    TEST_ASSERT_EQUAL(ESP_OK, i2s_out_stop());

    /* FAULTED */
    s_sim_writer = SIM_FAULTED;
    s_sim_fault_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, i2s_out_start());
    TEST_ASSERT_EQUAL(I2S_STATE_FAULTED, i2s_out_get_state());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, i2s_out_deinit());
}

void test_peak_ring_statistic_is_populated(void)
{
    init_idle();

    uint8_t buf[256] = {0};
    size_t written = i2s_out_write(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(sizeof(buf), written);

    i2s_out_stats_t stats;
    i2s_out_get_stats(&stats);
    TEST_ASSERT_EQUAL(sizeof(buf), stats.ring_peak);
}

void test_missing_clock_timeout_does_not_overwrite_last_error(void)
{
    /* A real driver ESP_ERR_TIMEOUT during WAITING_FOR_CLOCK must not
     * clobber last_error (spec §6.6) — verified at the unit level here via
     * the injection hook, matching what writer_task() actually does. */
    init_idle();
    s_sim_writer = SIM_RUNNING;
    TEST_ASSERT_EQUAL(ESP_OK, i2s_out_start());

    i2s_test_inject_writer_state(I2S_STATE_FAULTED, ESP_ERR_INVALID_ARG);
    i2s_out_stats_t before;
    i2s_out_get_stats(&before);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, before.last_error);

    i2s_test_inject_writer_state(I2S_STATE_WAITING_FOR_CLOCK, ESP_OK);
    i2s_out_stats_t after;
    i2s_out_get_stats(&after);
    TEST_ASSERT_EQUAL(I2S_STATE_WAITING_FOR_CLOCK, after.state);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, after.last_error);
}

/* ---- 2026-07-22 DMA-starvation regression proofs ----
 * The bug: i2s_channel_write() was handed pdMS_TO_TICKS(100) though the API
 * takes milliseconds (double conversion -> 1-tick timeout -> constant
 * spurious timeouts), and every timeout was treated as "no clock" with a
 * 100 ms nap — starving the DMA to ~1/3 of the wire rate while the hardware
 * replayed stale buffers as audible static. These tests drive the real
 * writer-loop body directly via i2s_test_writer_step(). */

/* Bring the module to RUNNING with a live (mocked) writer so writer_step()
 * has a ring/channel/event-group to operate on. */
static void init_running(void)
{
    init_idle();
    s_sim_writer = SIM_RUNNING;
    TEST_ASSERT_EQUAL(ESP_OK, i2s_out_start());
    TEST_ASSERT_EQUAL(I2S_STATE_RUNNING, i2s_out_get_state());
}

void test_write_timeout_is_milliseconds(void)
{
    /* This target compiles with configTICK_RATE_HZ=100, so
     * pdMS_TO_TICKS(100) == 10 != 100. If anyone reintroduces the
     * double conversion, the captured argument becomes 10 and this fails. */
    TEST_ASSERT_EQUAL_UINT32(10, pdMS_TO_TICKS(100)); /* sanity: mock tick rate */

    init_running();
    TEST_ASSERT_TRUE(i2s_test_writer_step());
    TEST_ASSERT_EQUAL_UINT32(100, s_write_last_timeout_arg);
}

void test_timeout_with_progress_keeps_running_and_never_naps(void)
{
    init_running();

    /* DMA accepted half the block within the window: the clock is provably
     * present. The writer must stay RUNNING and must NOT take the 100 ms
     * "no clock" nap (each nap = ~3 full stale-buffer replays on the wire). */
    mock_write_queue(ESP_ERR_TIMEOUT, 1024);
    TEST_ASSERT_TRUE(i2s_test_writer_step());
    TEST_ASSERT_EQUAL(I2S_STATE_RUNNING, i2s_out_get_state());
    TEST_ASSERT_EQUAL_UINT(0, i2s_test_backoff_naps());

    /* Follow-up full write drains the retained remainder. */
    TEST_ASSERT_TRUE(i2s_test_writer_step());
    TEST_ASSERT_EQUAL(I2S_STATE_RUNNING, i2s_out_get_state());
    TEST_ASSERT_EQUAL_UINT(0, i2s_test_backoff_naps());
}

void test_timeout_with_zero_written_backs_off_once(void)
{
    init_running();

    /* Zero bytes accepted for the whole window: genuinely no clock. The
     * writer must publish WAITING_FOR_CLOCK and take exactly one backoff
     * nap per step (never a tight retry loop — SPEC 6.4's 10 Hz cap). */
    mock_write_queue(ESP_ERR_TIMEOUT, 0);
    TEST_ASSERT_TRUE(i2s_test_writer_step());
    TEST_ASSERT_EQUAL(I2S_STATE_WAITING_FOR_CLOCK, i2s_out_get_state());
    TEST_ASSERT_EQUAL_UINT(1, i2s_test_backoff_naps());

    /* Clock returns: the very next successful write goes back to RUNNING. */
    TEST_ASSERT_TRUE(i2s_test_writer_step());
    TEST_ASSERT_EQUAL(I2S_STATE_RUNNING, i2s_out_get_state());
    TEST_ASSERT_EQUAL_UINT(1, i2s_test_backoff_naps());
}

void test_write_fault_stops_writer_loop(void)
{
    init_running();

    mock_write_queue(ESP_FAIL, 0);
    TEST_ASSERT_FALSE(i2s_test_writer_step());   /* loop must exit */
    TEST_ASSERT_EQUAL(I2S_STATE_FAULTED, i2s_out_get_state());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_writer_waiting_before_start_returns_and_state_stays_waiting);
    RUN_TEST(test_writer_faults_before_start_returns_and_state_stays_faulted);
    RUN_TEST(test_start_ack_timeout_cancel_succeeds_reaches_idle);
    RUN_TEST(test_start_ack_timeout_no_exit_becomes_join_pending);
    RUN_TEST(test_stop_timeout_retains_resources_and_blocks_deinit);
    RUN_TEST(test_disable_failure_leaves_faulted_channel_still_enabled);
    RUN_TEST(test_repeated_stop_retries_disable_and_reaches_idle);
    RUN_TEST(test_deinit_rejects_every_non_idle_state);
    RUN_TEST(test_peak_ring_statistic_is_populated);
    RUN_TEST(test_missing_clock_timeout_does_not_overwrite_last_error);
    /* 2026-07-22 DMA-starvation regression proofs */
    RUN_TEST(test_write_timeout_is_milliseconds);
    RUN_TEST(test_timeout_with_progress_keeps_running_and_never_naps);
    RUN_TEST(test_timeout_with_zero_written_backs_off_once);
    RUN_TEST(test_write_fault_stops_writer_loop);
    return UNITY_END();
}
