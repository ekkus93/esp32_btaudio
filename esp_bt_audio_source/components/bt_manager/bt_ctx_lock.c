/* bt_ctx_lock()/bt_ctx_unlock() live in their own translation unit, separate
 * from bt_manager.c's other ~25 production functions, so that any caller
 * needing just the lock (bt_manager_ops.c, bt_pairing_store.c, bt_connection.c,
 * bt_scan.c, bt_events_gap.c, bt_events_a2dp.c, the HFP duplex files) doesn't
 * force a static-library extraction of the whole bt_manager.c object file.
 * Test scaffolding (e.g. test_bluetooth's bt_manager_api_mock.c) mocks the
 * rest of bt_manager.c's public API; pulling in bt_manager.c.obj just for
 * bt_ctx_lock used to reintroduce those symbols and collide with the mocks. */

#include "bt_manager_internal.h"
#include "platform_sync.h"
#include "esp_err.h"
#include "esp_log.h"

#define TAG "BT_MGR"

/* Mutex that protects bt_ctx.  Writers (callbacks running on BtAppTask)
 * acquire the lock before modifying bt_ctx.  Readers (e.g.
 * bt_manager_get_status) acquire the lock to obtain a consistent
 * snapshot.  Callbacks are NOT invoked while holding the lock. */
platform_mutex_t s_bt_ctx_mutex;

#ifdef UNIT_TEST
/* One-shot failure injection at the bt_ctx locking boundary. The countdown
 * permits tests to fail a later production lock without adding caller-specific
 * test branches. A value of zero fails the next lock attempt. */
static esp_err_t s_bt_ctx_forced_next_lock_result = ESP_OK;
static uint32_t s_bt_ctx_forced_lock_countdown;
#endif

/* ============================================================================
 * bt_ctx lock/unlock helpers
 *
 * These acquire/release s_bt_ctx_mutex to protect bt_ctx from concurrent
 * reads and writes.  Must be called before accessing bt_ctx fields except
 * during init/deinit when the manager is not yet running.
 * ============================================================================ */

MAYBE_WEAK esp_err_t bt_ctx_lock(uint32_t timeout_ms) {
#ifdef UNIT_TEST
    if (s_bt_ctx_forced_next_lock_result != ESP_OK) {
        if (s_bt_ctx_forced_lock_countdown == 0U) {
            esp_err_t forced = s_bt_ctx_forced_next_lock_result;
            s_bt_ctx_forced_next_lock_result = ESP_OK;
            return forced;
        }
        s_bt_ctx_forced_lock_countdown--;
    }
#endif
    if (s_bt_ctx_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return platform_mutex_lock(s_bt_ctx_mutex, timeout_ms);
}

MAYBE_WEAK void bt_ctx_unlock(void) {
    esp_err_t err = platform_mutex_unlock(s_bt_ctx_mutex);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bt_ctx unlock failed: %s", esp_err_to_name(err));
    }
}

#ifdef UNIT_TEST
esp_err_t bt_manager_test_init_mutex(void)
{
    if (s_bt_ctx_mutex) platform_mutex_delete(s_bt_ctx_mutex);
    s_bt_ctx_mutex = platform_mutex_create();
    if (s_bt_ctx_mutex == NULL) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void bt_manager_test_deinit_mutex(void)
{
    if (s_bt_ctx_mutex) {
        platform_mutex_delete(s_bt_ctx_mutex);
        s_bt_ctx_mutex = NULL;
    }
}

void bt_manager_test_force_next_ctx_lock_result(esp_err_t result)
{
    s_bt_ctx_forced_lock_countdown = 0U;
    s_bt_ctx_forced_next_lock_result = result;
}

void bt_manager_test_force_ctx_lock_after_successes(
    uint32_t successful_locks_before_failure,
    esp_err_t result)
{
    s_bt_ctx_forced_lock_countdown = successful_locks_before_failure;
    s_bt_ctx_forced_next_lock_result = result;
}
#endif
