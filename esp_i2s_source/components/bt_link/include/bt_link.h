/*
 * bt_link — UART1 command client to the WROOM32 UART2 port (LINK-1).
 * Device API: one bt_link task owns UART1 + the session; callers issue
 * synchronous commands and register EVENT subscribers. S3 UART1
 * TX=GPIO17 -> WROOM32 GPIO16, RX=GPIO18 <- WROOM32 GPIO17 (SPEC §3.2).
 *
 * The pure logic lives in bt_link_parser.h / bt_link_session.h (host-tested).
 * This device task is verified on hardware at LINK-1c.
 */
#pragma once

#include "bt_link_parser.h"
#include "bt_link_session.h"

#ifdef ESP_PLATFORM
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_LINK_UART_TX_GPIO   17
#define BT_LINK_UART_RX_GPIO   18
#define BT_LINK_DEFAULT_TIMEOUT_MS 2000

/* Install UART1, init the session, and spawn the bt_link task. Idempotent:
 * a repeat call with the same cmd_timeout_ms returns ESP_OK without
 * reinstalling UART; a repeat call with a different timeout returns
 * ESP_ERR_INVALID_STATE. */
esp_err_t bt_link_init(uint32_t cmd_timeout_ms);

/* Signal both the UART task and the event dispatch task to exit, and wait
 * (bounded) for them to acknowledge. Resources are retained if the wait
 * times out (ESP_ERR_TIMEOUT). Call before bt_link_deinit(). */
esp_err_t bt_link_stop(void);

/* Release UART/queue/mutex resources. Requires both tasks to have already
 * exited (call bt_link_stop() first) — returns ESP_ERR_INVALID_STATE
 * otherwise. Idempotent. */
esp_err_t bt_link_deinit(void);

bool bt_link_is_initialized(void);

/* Register an EVENT subscriber. Callbacks run on a dedicated dispatch task
 * (never the UART owner task), so a callback may safely call bt_link_send()
 * itself without deadlocking. */
int bt_link_subscribe(bt_link_event_cb cb, void *ctx);

/* Remove a previously registered subscriber. Returns the freed slot index,
 * or -1 if not found. */
int bt_link_unsubscribe(bt_link_event_cb cb, void *ctx);

/* Send `cmd` and block until its terminal response or timeout. On return,
 * *out_state is DONE_OK / DONE_ERR / TIMEOUT and `result` holds the response
 * RESULT field (truncated to result_sz). Returns ESP_OK if a terminal
 * response arrived, ESP_ERR_TIMEOUT otherwise. Serialized (one in flight). */
esp_err_t bt_link_send(const char *cmd, bt_link_cmd_state_t *out_state,
                       char *result, size_t result_sz,
                       char *data, size_t data_sz);

#ifdef UNIT_TEST
/* Test injection hooks (TODO 4.8). A mocked xTaskCreate() does not run the
 * real task bodies, so host tests simulate what they would publish via
 * these — same technique as i2s_out.c's i2s_test_inject_writer_bits().
 * uint32_t (not EventBits_t) to keep FreeRTOS types out of this header. */
void bt_link_test_inject_lifecycle_bits(uint32_t bits);
#define BT_LINK_TEST_EVT_TASK_ENTERED       ((uint32_t)1)
#define BT_LINK_TEST_EVT_EVENT_TASK_ENTERED ((uint32_t)2)
#define BT_LINK_TEST_EVT_TASK_EXITED        ((uint32_t)4)
#define BT_LINK_TEST_EVT_EVENT_TASK_EXITED  ((uint32_t)8)

/* Move one request from s_cmd_queue to s_active, mimicking what
 * bt_link_task()'s loop does right before writing to the UART — lets a
 * test exercise "an active request gets cancelled on shutdown" (BTLINK-002)
 * without the real task ever running. Returns true if a request was moved. */
bool bt_link_test_move_one_queued_to_active(void);

/* Directly invoke the same cancellation logic bt_link_task() runs at exit:
 * complete s_active (if any) and every still-queued request with a local
 * cancellation error, releasing the worker's reference on each exactly
 * once. */
void bt_link_test_invoke_cancel_active_and_queued(void);

/* Force every module static back to pre-init state regardless of current
 * lifecycle state — test isolation only (never compiled into production),
 * same rationale as i2s_out.c's i2s_test_reset_module_state(). */
void bt_link_test_reset_module_state(void);
#endif /* UNIT_TEST */

#ifdef __cplusplus
}
#endif
#endif /* ESP_PLATFORM */
