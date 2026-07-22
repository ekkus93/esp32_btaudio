/* bt_link (device glue) — UART1 command task (LINK-1b). One task owns the
 * UART and the session; bt_link_send() hands it a heap-allocated,
 * two-owner-refcounted request and blocks on the request's own completion
 * semaphore. Event callbacks run on a dedicated dispatch task, never on the
 * UART owner task, so a callback that itself calls bt_link_send() cannot
 * deadlock (TODO 4.5 / BTLINK-011).
 *
 * TODO Phase 4 rewrite:
 *   4.1 - idempotent init/stop/deinit, bt_link_is_initialized().
 *   4.2 - refs-based request ownership (retain-before-enqueue) replaces the
 *         `abandoned` flag, which had a caller/worker use-after-free race
 *         (BTLINK-001): the caller could free the request immediately after
 *         the worker signalled it, before the worker's own `abandoned` read.
 *   4.3 - commands are validated (non-null, non-empty, no CR/LF/NUL/control
 *         bytes, fits BT_LINK_LINE_MAX-1) and rejected outright rather than
 *         silently truncated (BTLINK-005 — truncation also let embedded
 *         CR/LF smuggle a second command through the raw web terminal).
 *   4.4 - the UART write return value is checked instead of ignored
 *         (BTLINK-007).
 *   4.5 - event dispatch moves off the UART task via a queue + dedicated
 *         task; events are deep-copied, never pointers into the reusable
 *         line buffer (BTLINK-011/BTLINK-013); bt_link_unsubscribe() added
 *         (BTLINK-010).
 */
#include "bt_link.h"

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static const char *TAG = "bt_link";

#define BT_LINK_UART        UART_NUM_1
#define BT_LINK_RX_BUF      1024
#define BT_LINK_TASK_STACK  4096
#define BT_LINK_TASK_PRIO   (configMAX_PRIORITIES - 4)
#define BT_LINK_EVENT_TASK_STACK 3072
#define BT_LINK_EVENT_TASK_PRIO  (configMAX_PRIORITIES - 5)
#define BT_LINK_EVENT_QUEUE_LEN  8

/* Explicit lifecycle state (TODO 4.1). FAULTED_JOIN_PENDING means one or
 * both worker tasks may still be running and may still touch
 * s_active/s_cmd_queue/s_event_queue/s_subs — deinit() is forbidden in
 * this state; only a retried stop() may attempt the join again. */
typedef enum {
    BT_LINK_STATE_UNINITIALIZED = 0,
    BT_LINK_STATE_STARTING,
    BT_LINK_STATE_RUNNING,
    BT_LINK_STATE_STOPPING,
    BT_LINK_STATE_STOPPED,
    BT_LINK_STATE_FAULTED_JOIN_PENDING,
} bt_link_state_t;

static _Atomic bt_link_state_t s_lifecycle_state = BT_LINK_STATE_UNINITIALIZED;

/* Two-owner refcounted request (TODO 4.2). refs starts at 1 (caller).
 * request_retain() bumps it to 2 to reserve the worker's eventual ownership
 * *before* the item is handed to the queue — if the enqueue itself fails,
 * both references are released synchronously and nothing is left for a
 * worker that will never see the item. Once enqueued, the worker owns that
 * second reference and is the only one allowed to release it; the worker
 * never reads the request after its own release. */
typedef struct {
    atomic_uint             refs;
    char                    cmd[BT_LINK_LINE_MAX];
    bt_link_cmd_state_t     state;
    esp_err_t               transport_err;  /* local transport result (TODO 4.2/4.3) */
    char                    result[BT_LINK_FIELD_MAX];
    char                    data[BT_LINK_FIELD_MAX];
    SemaphoreHandle_t       done_sem;
} bt_link_request_t;

static bt_link_request_t *request_create(const char *cmd)
{
    bt_link_request_t *req = (bt_link_request_t *)calloc(1, sizeof(*req));
    if (!req) return NULL;
    strncpy(req->cmd, cmd, BT_LINK_LINE_MAX - 1);
    req->done_sem = xSemaphoreCreateBinary();
    if (!req->done_sem) {
        free(req);
        return NULL;
    }
    req->transport_err = ESP_OK;
    atomic_init(&req->refs, 1);
    return req;
}

static void request_retain(bt_link_request_t *req)
{
    atomic_fetch_add(&req->refs, 1);
}

/* Releases one reference; frees on the last release. Never touches *req
 * after the count could reach zero from another thread's release, so the
 * decrement result is checked locally before any further access. */
static void request_release(bt_link_request_t *req)
{
    if (atomic_fetch_sub(&req->refs, 1) == 1) {
        vSemaphoreDelete(req->done_sem);
        free(req);
    }
}

/* One completion path for every way the worker finishes a request — normal
 * terminal response, local UART transport failure, or shutdown cancellation
 * (TODO 4.2). Called exactly once per request from the worker side. Writes
 * fields, signals the caller's semaphore, then releases the worker's own
 * reference (which may free the request if the caller already gave up). */
static void request_complete_worker(bt_link_request_t *req, esp_err_t transport_err,
                                    bt_link_cmd_state_t state,
                                    const char *result, const char *data)
{
    if (!req) return;
    req->transport_err = transport_err;
    req->state = state;
    if (result) strncpy(req->result, result, sizeof(req->result) - 1);
    if (data) strncpy(req->data, data, sizeof(req->data) - 1);
    xSemaphoreGive(req->done_sem);
    request_release(req);
}

/* Deep-copied event item (TODO 4.5/BTLINK-013): never a pointer into the
 * reusable UART line buffer. Sized to the same fields bt_link_msg_t exposes. */
typedef struct {
    bt_link_status_t status;
    char status_str[BT_LINK_VERB_MAX];
    char command[BT_LINK_VERB_MAX];
    char result[BT_LINK_FIELD_MAX];
    char data[BT_LINK_FIELD_MAX];
} bt_link_event_item_t;

typedef struct {
    bt_link_event_cb cb;
    void             *ctx;
    bool             used;
} bt_link_sub_t;

static bt_link_session_t  s_session;
static bt_link_linebuf_t  s_linebuf;
static QueueHandle_t      s_cmd_queue;   /* bt_link_request_t* */
static SemaphoreHandle_t  s_send_mutex;
static bt_link_request_t *s_active;
static uint32_t           s_cmd_timeout_ms; /* configured command timeout */

static TaskHandle_t       s_task;
static TaskHandle_t       s_event_task;
static _Atomic bool       s_stop_requested;
static EventGroupHandle_t s_lifecycle_events;
/* ENTERED distinguishes "task scheduled and running" from task-creation
 * success alone, so init() can wait for real task entry before publishing
 * RUNNING (TODO 4.1/4.2), matching the same ENTERED/EXITED split used for
 * i2s_out.c and radio.c elsewhere in this codebase. */
#define BT_LINK_EVT_TASK_ENTERED       (1u << 0)
#define BT_LINK_EVT_EVENT_TASK_ENTERED (1u << 1)
#define BT_LINK_EVT_TASK_EXITED        (1u << 2)
#define BT_LINK_EVT_EVENT_TASK_EXITED  (1u << 3)

static QueueHandle_t      s_event_queue;
static SemaphoreHandle_t  s_subs_mtx;
static bt_link_sub_t      s_subs[BT_LINK_MAX_SUBSCRIBERS];

/* TODO 4.3: reject, never silently truncate or accept embedded CR/LF/NUL. */
static bool command_is_valid(const char *cmd)
{
    if (!cmd || !*cmd) return false;
    size_t len = strlen(cmd);
    if (len >= BT_LINK_LINE_MAX) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)cmd[i];
        if (c == '\r' || c == '\n' || c == '\0' || iscntrl(c)) return false;
    }
    return true;
}

/* Relay registered once with the session; runs on the UART owner task, so it
 * must never block. Deep-copies the event and hands it to the dispatch
 * queue; drops (with a rate-limited log) if the queue is momentarily full
 * rather than ever blocking the UART reader. */
static void event_relay(void *ctx, const bt_link_msg_t *evt)
{
    (void)ctx;
    if (!s_event_queue || !evt) return;

    bt_link_event_item_t item = {0};
    item.status = evt->status;
    if (evt->status_str) {
        strncpy(item.status_str, evt->status_str, sizeof(item.status_str) - 1);
    }
    if (evt->command) {
        strncpy(item.command, evt->command, sizeof(item.command) - 1);
    }
    if (evt->result) {
        strncpy(item.result, evt->result, sizeof(item.result) - 1);
    }
    if (evt->data) {
        strncpy(item.data, evt->data, sizeof(item.data) - 1);
    }

    if (xQueueSend(s_event_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, dropping event status=%d", (int)evt->status);
    }
}

static void event_dispatch_task(void *arg)
{
    (void)arg;
    bt_link_event_item_t item;

    xEventGroupSetBits(s_lifecycle_events, BT_LINK_EVT_EVENT_TASK_ENTERED);

    while (!atomic_load(&s_stop_requested)) {
        if (xQueueReceive(s_event_queue, &item, pdMS_TO_TICKS(200)) != pdTRUE) {
            continue;
        }

        bt_link_msg_t evt = {
            .status = item.status,
            .status_str = item.status_str,
            .command = item.command,
            .result = item.result,
            .data = item.data,
        };

        /* Copy the subscriber list under the lock, then invoke callbacks
         * with the lock released (TODO 4.5: never call callbacks while
         * holding the subscriber mutex). */
        bt_link_sub_t snapshot[BT_LINK_MAX_SUBSCRIBERS];
        xSemaphoreTake(s_subs_mtx, portMAX_DELAY);
        memcpy(snapshot, s_subs, sizeof(snapshot));
        xSemaphoreGive(s_subs_mtx);

        for (int i = 0; i < BT_LINK_MAX_SUBSCRIBERS; i++) {
            if (snapshot[i].used && snapshot[i].cb) {
                snapshot[i].cb(snapshot[i].ctx, &evt);
            }
        }
    }

    xEventGroupSetBits(s_lifecycle_events, BT_LINK_EVT_EVENT_TASK_EXITED);
    s_event_task = NULL;
    vTaskDelete(NULL);
}

static void on_line(void *ctx, char *line)
{
    (void)ctx;
    bt_link_msg_t m;
    if (bt_link_parse_line(line, &m)) {
        bt_link_session_on_message(&s_session, &m);
    }
}

/* Complete s_active (if any) and drain+complete every request still queued,
 * each with a local cancellation error — called once, at worker exit
 * (TODO 4.4 / BTLINK-002). Previously the drain only released the worker's
 * reference on queued requests without ever signaling their semaphore or
 * touching s_active at all, so callers blocked for their full timeout
 * instead of failing fast, and an abandoned s_active request leaked (only
 * one of its two references was ever released). */
static void cancel_active_and_queued(void)
{
    if (s_active) {
        bt_link_request_t *active = s_active;
        s_active = NULL;
        request_complete_worker(active, ESP_ERR_INVALID_STATE, BT_LINK_CMD_TIMEOUT,
                                "CANCELLED", "shutdown");
    }

    bt_link_request_t *pending;
    while (xQueueReceive(s_cmd_queue, &pending, 0) == pdTRUE) {
        request_complete_worker(pending, ESP_ERR_INVALID_STATE, BT_LINK_CMD_TIMEOUT,
                                "CANCELLED", "shutdown");
    }
}

#ifdef UNIT_TEST
void bt_link_test_inject_lifecycle_bits(uint32_t bits)
{
    if (s_lifecycle_events) {
        xEventGroupSetBits(s_lifecycle_events, (EventBits_t)bits);
    }
}

bool bt_link_test_move_one_queued_to_active(void)
{
    if (s_active) return false;
    bt_link_request_t *req = NULL;
    if (xQueueReceive(s_cmd_queue, &req, 0) != pdTRUE || !req) return false;
    s_active = req;
    return true;
}

void bt_link_test_invoke_cancel_active_and_queued(void)
{
    cancel_active_and_queued();
}

void bt_link_test_reset_module_state(void)
{
    s_task = NULL;
    s_event_task = NULL;
    if (s_cmd_queue) {
        cancel_active_and_queued(); /* completes s_active too, if any */
    } else {
        s_active = NULL;
    }
    if (s_lifecycle_events) { vEventGroupDelete(s_lifecycle_events); s_lifecycle_events = NULL; }
    if (s_event_queue) { vQueueDelete(s_event_queue); s_event_queue = NULL; }
    if (s_subs_mtx) { vSemaphoreDelete(s_subs_mtx); s_subs_mtx = NULL; }
    if (s_send_mutex) { vSemaphoreDelete(s_send_mutex); s_send_mutex = NULL; }
    if (s_cmd_queue) { vQueueDelete(s_cmd_queue); s_cmd_queue = NULL; }
    memset(s_subs, 0, sizeof(s_subs));
    atomic_store(&s_stop_requested, false);
    atomic_store(&s_lifecycle_state, BT_LINK_STATE_UNINITIALIZED);
}
#endif /* UNIT_TEST */

static void bt_link_task(void *arg)
{
    (void)arg;
    uint8_t rx[256];
    TickType_t last = xTaskGetTickCount();

    xEventGroupSetBits(s_lifecycle_events, BT_LINK_EVT_TASK_ENTERED);

    while (!atomic_load(&s_stop_requested)) {
        /* Start the next queued command when nothing is in flight. */
        if (s_active == NULL) {
            bt_link_request_t *req = NULL;
            if (xQueueReceive(s_cmd_queue, &req, 0) == pdTRUE && req) {
                bt_link_session_begin(&s_session, req->cmd);
                char out[BT_LINK_LINE_MAX + 2];
                int n = snprintf(out, sizeof(out), "%s\r\n", req->cmd);
                int written = (n > 0) ? uart_write_bytes(BT_LINK_UART, out, (size_t)n) : -1;
                if (written != n) {
                    /* Local transport failure — complete immediately rather
                     * than waiting for the peer to never respond (TODO 4.3 /
                     * BTLINK-004: this used to only log, letting the caller
                     * discover the failure via the ordinary session
                     * timeout). */
                    ESP_LOGW(TAG, "uart_write_bytes short/failed: wrote %d of %d", written, n);
                    request_complete_worker(req, ESP_FAIL, BT_LINK_CMD_TIMEOUT,
                                            "LOCAL_UART_WRITE_FAILED", "");
                } else {
                    s_active = req;
                }
            }
        }

        int r = uart_read_bytes(BT_LINK_UART, rx, sizeof(rx), pdMS_TO_TICKS(20));
        if (r > 0) {
            bt_link_linebuf_feed(&s_linebuf, rx, (size_t)r, on_line, NULL);
        }

        TickType_t now = xTaskGetTickCount();
        uint32_t dt = (uint32_t)((now - last) * portTICK_PERIOD_MS);
        last = now;
        bt_link_session_tick(&s_session, dt);

        if (s_active) {
            bt_link_cmd_state_t st = bt_link_session_state(&s_session);
            if (st == BT_LINK_CMD_DONE_OK || st == BT_LINK_CMD_DONE_ERR ||
                st == BT_LINK_CMD_TIMEOUT) {
                bt_link_request_t *req = s_active;
                s_active = NULL;
                request_complete_worker(req, ESP_OK, st,
                                        s_session.last_result, s_session.last_data);
            }
        }
    }

    /* Complete (not just release) s_active and every still-queued request
     * so no caller is left blocked until its own timeout (TODO 4.4). */
    cancel_active_and_queued();

    xEventGroupSetBits(s_lifecycle_events, BT_LINK_EVT_TASK_EXITED);
    s_task = NULL;
    vTaskDelete(NULL);
}

bool bt_link_is_initialized(void)
{
    return atomic_load(&s_lifecycle_state) != BT_LINK_STATE_UNINITIALIZED;
}

esp_err_t bt_link_init(uint32_t cmd_timeout_ms)
{
    uint32_t timeout = cmd_timeout_ms ? cmd_timeout_ms : BT_LINK_DEFAULT_TIMEOUT_MS;

    bt_link_state_t existing = atomic_load(&s_lifecycle_state);
    if (existing == BT_LINK_STATE_RUNNING) {
        /* Genuinely idempotent only while actually running. */
        return (timeout == s_cmd_timeout_ms) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (existing != BT_LINK_STATE_UNINITIALIZED) {
        /* STARTING/STOPPING/STOPPED/FAULTED_JOIN_PENDING: never silently
         * "OK" even with a matching timeout — the module isn't safely
         * running and needs stop()/retry to resolve, not a repeated init(). */
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store(&s_lifecycle_state, BT_LINK_STATE_STARTING);

    esp_err_t err;
    bool uart_installed = false;

    const uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    err = uart_driver_install(BT_LINK_UART, BT_LINK_RX_BUF, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        atomic_store(&s_lifecycle_state, BT_LINK_STATE_UNINITIALIZED);
        return err;
    }
    uart_installed = true;

    err = uart_param_config(BT_LINK_UART, &cfg);
    if (err != ESP_OK) goto fail;
    err = uart_set_pin(BT_LINK_UART, BT_LINK_UART_TX_GPIO, BT_LINK_UART_RX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) goto fail;
    /* The uart_set_pin transition can glitch the TX line, leaving a partial
     * garbage line in the peer's RX assembler that would swallow the first
     * real command (observed at LINK-1c: first VERSION timed out, rest OK).
     * Send a lone CRLF so the peer terminates and discards that stray line. */
    uart_flush_input(BT_LINK_UART);
    uart_write_bytes(BT_LINK_UART, "\r\n", 2);

    s_cmd_timeout_ms = timeout;
    bt_link_session_init(&s_session, s_cmd_timeout_ms);
    bt_link_linebuf_init(&s_linebuf);
    bt_link_session_subscribe(&s_session, event_relay, NULL);
    memset(s_subs, 0, sizeof(s_subs));

    s_cmd_queue = xQueueCreate(4, sizeof(bt_link_request_t *));
    s_send_mutex = xSemaphoreCreateMutex();
    s_subs_mtx = xSemaphoreCreateMutex();
    s_event_queue = xQueueCreate(BT_LINK_EVENT_QUEUE_LEN, sizeof(bt_link_event_item_t));
    s_lifecycle_events = xEventGroupCreate();
    if (!s_cmd_queue || !s_send_mutex || !s_subs_mtx || !s_event_queue || !s_lifecycle_events) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    atomic_store(&s_stop_requested, false);

    if (xTaskCreate(event_dispatch_task, "bt_link_evt", BT_LINK_EVENT_TASK_STACK, NULL,
                    BT_LINK_EVENT_TASK_PRIO, &s_event_task) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    if (xTaskCreate(bt_link_task, "bt_link", BT_LINK_TASK_STACK, NULL,
                    BT_LINK_TASK_PRIO, &s_task) != pdPASS) {
        atomic_store(&s_stop_requested, true);
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    /* Wait for task-entry acknowledgement before publishing RUNNING (TODO
     * 4.1/4.2) — task-creation success alone doesn't prove either task has
     * actually run yet. */
    {
        EventBits_t bits = xEventGroupWaitBits(
            s_lifecycle_events,
            BT_LINK_EVT_TASK_ENTERED | BT_LINK_EVT_EVENT_TASK_ENTERED,
            pdFALSE, pdTRUE, pdMS_TO_TICKS(1000));
        if ((bits & (BT_LINK_EVT_TASK_ENTERED | BT_LINK_EVT_EVENT_TASK_ENTERED)) !=
            (BT_LINK_EVT_TASK_ENTERED | BT_LINK_EVT_EVENT_TASK_ENTERED)) {
            atomic_store(&s_stop_requested, true);
            err = ESP_ERR_TIMEOUT;
            goto fail;
        }
    }

    atomic_store(&s_lifecycle_state, BT_LINK_STATE_RUNNING);
    ESP_LOGI(TAG, "init: UART1 tx=%d rx=%d @115200, timeout=%ums",
             BT_LINK_UART_TX_GPIO, BT_LINK_UART_RX_GPIO,
             (unsigned)s_cmd_timeout_ms);
    return ESP_OK;

fail:
    /* Join-safety (TODO 4.6 / BTLINK-001): if either task was actually
     * created, it must be given a chance to exit before its shared
     * resources are deleted out from under it. stop_requested is already
     * true by the time we reach here whenever a task exists. */
    if (s_event_task || s_task) {
        EventBits_t want = (s_event_task ? BT_LINK_EVT_EVENT_TASK_EXITED : 0) |
                           (s_task ? BT_LINK_EVT_TASK_EXITED : 0);
        EventBits_t got = xEventGroupWaitBits(s_lifecycle_events, want,
                                              pdFALSE, pdTRUE, pdMS_TO_TICKS(1000));
        if ((got & want) != want) {
            /* The join itself is now the dominant problem — ownership is
             * unresolved, which matters more to the caller than the
             * original creation-failure cause (matches i2s_out.c's
             * ack-timeout-then-join-timeout precedent). */
            ESP_LOGW(TAG, "init failed and join timed out — resources retained");
            atomic_store(&s_lifecycle_state, BT_LINK_STATE_FAULTED_JOIN_PENDING);
            return ESP_ERR_TIMEOUT;
        }
    }
    if (s_lifecycle_events) { vEventGroupDelete(s_lifecycle_events); s_lifecycle_events = NULL; }
    if (s_event_queue) { vQueueDelete(s_event_queue); s_event_queue = NULL; }
    if (s_subs_mtx) { vSemaphoreDelete(s_subs_mtx); s_subs_mtx = NULL; }
    if (s_send_mutex) { vSemaphoreDelete(s_send_mutex); s_send_mutex = NULL; }
    if (s_cmd_queue) { vQueueDelete(s_cmd_queue); s_cmd_queue = NULL; }
    if (uart_installed) uart_driver_delete(BT_LINK_UART);
    atomic_store(&s_lifecycle_state, BT_LINK_STATE_UNINITIALIZED);
    return err;
}

esp_err_t bt_link_stop(void)
{
    bt_link_state_t state = atomic_load(&s_lifecycle_state);
    if (state == BT_LINK_STATE_UNINITIALIZED || state == BT_LINK_STATE_STOPPED) {
        return ESP_OK;   /* idempotent */
    }
    /* Legal from STARTING (cancel an in-progress init's tasks — though
     * init() itself already handles its own join-safety), RUNNING,
     * STOPPING (a retry of a prior call), and FAULTED_JOIN_PENDING (retry
     * a previously-timed-out join) — TODO 4.7. */

    atomic_store(&s_lifecycle_state, BT_LINK_STATE_STOPPING);
    atomic_store(&s_stop_requested, true);

    EventBits_t bits = xEventGroupWaitBits(
        s_lifecycle_events,
        BT_LINK_EVT_TASK_EXITED | BT_LINK_EVT_EVENT_TASK_EXITED,
        pdFALSE, pdTRUE, pdMS_TO_TICKS(1000));
    if ((bits & (BT_LINK_EVT_TASK_EXITED | BT_LINK_EVT_EVENT_TASK_EXITED)) !=
        (BT_LINK_EVT_TASK_EXITED | BT_LINK_EVT_EVENT_TASK_EXITED)) {
        ESP_LOGW(TAG, "stop: task exit timed out, resources retained");
        atomic_store(&s_lifecycle_state, BT_LINK_STATE_FAULTED_JOIN_PENDING);
        return ESP_ERR_TIMEOUT;
    }
    /* Lifecycle owner clears the task handles once EXITED is confirmed —
     * the worker tasks are not the sole source of truth for this (TODO
     * 4.6); they also clear their own handle just before self-deleting,
     * which is harmless/redundant on real hardware but this is the only
     * place that's guaranteed to run. */
    s_task = NULL;
    s_event_task = NULL;
    atomic_store(&s_lifecycle_state, BT_LINK_STATE_STOPPED);
    return ESP_OK;
}

esp_err_t bt_link_deinit(void)
{
    bt_link_state_t state = atomic_load(&s_lifecycle_state);
    if (state == BT_LINK_STATE_UNINITIALIZED) return ESP_OK;
    /* Legal only from STOPPED — both tasks have confirmed exit and no
     * request is left in flight. Rejects STARTING/RUNNING/STOPPING and,
     * critically, FAULTED_JOIN_PENDING: a worker may still be running and
     * may still touch these resources (TODO 4.7). */
    if (state != BT_LINK_STATE_STOPPED) return ESP_ERR_INVALID_STATE;
    if (s_task || s_event_task) return ESP_ERR_INVALID_STATE;

    uart_driver_delete(BT_LINK_UART);
    if (s_lifecycle_events) { vEventGroupDelete(s_lifecycle_events); s_lifecycle_events = NULL; }
    if (s_event_queue) { vQueueDelete(s_event_queue); s_event_queue = NULL; }
    if (s_subs_mtx) { vSemaphoreDelete(s_subs_mtx); s_subs_mtx = NULL; }
    if (s_send_mutex) { vSemaphoreDelete(s_send_mutex); s_send_mutex = NULL; }
    if (s_cmd_queue) { vQueueDelete(s_cmd_queue); s_cmd_queue = NULL; }
    s_active = NULL;
    atomic_store(&s_lifecycle_state, BT_LINK_STATE_UNINITIALIZED);
    return ESP_OK;
}

int bt_link_subscribe(bt_link_event_cb cb, void *ctx)
{
    if (!s_subs_mtx || !cb) return -1;
    xSemaphoreTake(s_subs_mtx, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < BT_LINK_MAX_SUBSCRIBERS; i++) {
        if (!s_subs[i].used) { slot = i; break; }
    }
    if (slot >= 0) {
        s_subs[slot].cb = cb;
        s_subs[slot].ctx = ctx;
        s_subs[slot].used = true;
    }
    xSemaphoreGive(s_subs_mtx);
    return slot;
}

int bt_link_unsubscribe(bt_link_event_cb cb, void *ctx)
{
    if (!s_subs_mtx || !cb) return -1;
    xSemaphoreTake(s_subs_mtx, portMAX_DELAY);
    int removed = -1;
    for (int i = 0; i < BT_LINK_MAX_SUBSCRIBERS; i++) {
        if (s_subs[i].used && s_subs[i].cb == cb && s_subs[i].ctx == ctx) {
            s_subs[i].used = false;
            s_subs[i].cb = NULL;
            s_subs[i].ctx = NULL;
            removed = i;
            break;
        }
    }
    xSemaphoreGive(s_subs_mtx);
    return removed;
}

esp_err_t bt_link_send(const char *cmd, bt_link_cmd_state_t *out_state,
                       char *result, size_t result_sz,
                       char *data, size_t data_sz)
{
    if (!s_send_mutex) return ESP_ERR_INVALID_STATE;
    if (!command_is_valid(cmd)) return ESP_ERR_INVALID_ARG;
    /* Gate on RUNNING before taking the send lock (TODO 4.5/BTLINK-003) —
     * closes the window where STOPPING has already been published but the
     * queue could still silently accept a request no worker will ever
     * consume. */
    if (atomic_load(&s_lifecycle_state) != BT_LINK_STATE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_send_mutex, portMAX_DELAY);

    /* Re-check after acquiring the lock — closes the race where stop()
     * begins between the check above and taking the mutex. */
    if (atomic_load(&s_lifecycle_state) != BT_LINK_STATE_RUNNING) {
        xSemaphoreGive(s_send_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    bt_link_request_t *req = request_create(cmd);
    if (!req) {
        xSemaphoreGive(s_send_mutex);
        return ESP_ERR_NO_MEM;
    }

    /* Reserve the worker's eventual reference BEFORE the item is visible to
     * it, so a completing worker can never observe refs==1 and free the
     * request out from under an enqueue that hasn't finished yet. */
    request_retain(req);

    if (xQueueSend(s_cmd_queue, &req, pdMS_TO_TICKS(250)) != pdTRUE) {
        /* Never enqueued — the worker will never see this request. Release
         * both the reserved worker reference and our own. */
        request_release(req);
        request_release(req);
        xSemaphoreGive(s_send_mutex);
        return ESP_ERR_TIMEOUT;
    }

    /* Block on this request's own semaphore. The worker signals it after
     * writing state/result/data, then releases its reference. */
    if (xSemaphoreTake(req->done_sem, pdMS_TO_TICKS(s_cmd_timeout_ms + 500)) != pdTRUE) {
        /* Caller gives up — release only the caller's own reference. The
         * worker still owns its reference and will release it (and free the
         * request, if that's the last one) whenever it completes/cancels. */
        request_release(req);
        xSemaphoreGive(s_send_mutex);
        return ESP_ERR_TIMEOUT;
    }

    /* Worker has written req->state/result/data (synced by semaphore
     * barrier) and already released its own reference — we still hold ours,
     * so req is guaranteed to still be alive here. */
    bt_link_cmd_state_t final_state = req->state;
    esp_err_t ret = (final_state == BT_LINK_CMD_TIMEOUT) ? ESP_ERR_TIMEOUT : ESP_OK;

    if (out_state) *out_state = req->state;
    if (result && result_sz) {
        strncpy(result, req->result, result_sz - 1);
        result[result_sz - 1] = '\0';
    }
    if (data && data_sz) {
        strncpy(data, req->data, data_sz - 1);
        data[data_sz - 1] = '\0';
    }

    request_release(req);  /* our reference — last one, frees the request */
    xSemaphoreGive(s_send_mutex);
    return ret;
}
