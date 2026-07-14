/* bt_link (device glue) — UART1 command task (LINK-1b). One task owns the
 * UART and the session; bt_link_send() hands it a heap-allocated request and
 * blocks on the request's own completion semaphore. Per-request lifetimes
 * prevent stack-pointer-in-queue and cross-signaling (RH-S3-01).
 */
#include "bt_link.h"

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "bt_link";

#define BT_LINK_UART        UART_NUM_1
#define BT_LINK_RX_BUF      1024
#define BT_LINK_TASK_STACK  4096
#define BT_LINK_TASK_PRIO   (configMAX_PRIORITIES - 4)

/* One heap-allocated instance per bt_link_send() call. Carries its own
 * completion semaphore so the worker signals the right waiter.
 *
 * Lifetime:
 *   - Caller allocates, queues to worker, waits on req->done_sem
 *   - Worker signals req->done_sem when UART response arrives
 *   - Caller reads req fields, deletes semaphore, frees memory
 *   - If caller times out (abandoned), worker deletes semaphore + frees
 */
typedef struct {
    char                    cmd[BT_LINK_LINE_MAX];
    bt_link_cmd_state_t     state;
    char                    result[BT_LINK_FIELD_MAX];
    char                    data[BT_LINK_FIELD_MAX];
    SemaphoreHandle_t       done_sem;  /* per-request completion semaphore */
    bool                    abandoned; /* caller timed out — worker cleans up */
} bt_link_request_t;

static bt_link_session_t  s_session;
static bt_link_linebuf_t  s_linebuf;
static QueueHandle_t      s_cmd_queue;   /* bt_link_request_t* */
static SemaphoreHandle_t  s_send_mutex;
static bt_link_request_t *s_active;

static void on_line(void *ctx, char *line)
{
    (void)ctx;
    bt_link_msg_t m;
    if (bt_link_parse_line(line, &m)) {
        bt_link_session_on_message(&s_session, &m);
    }
}

static void bt_link_task(void *arg)
{
    (void)arg;
    uint8_t rx[256];
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        /* Start the next queued command when nothing is in flight. */
        if (s_active == NULL) {
            bt_link_request_t *req = NULL;
            if (xQueueReceive(s_cmd_queue, &req, 0) == pdTRUE && req) {
                s_active = req;
                bt_link_session_begin(&s_session, req->cmd);
                char out[BT_LINK_LINE_MAX + 2];
                int n = snprintf(out, sizeof(out), "%s\r\n", req->cmd);
                uart_write_bytes(BT_LINK_UART, out, n);
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

                /* Capture state before caller may read (synced by semaphore). */
                if (!req->abandoned) {
                    req->state = st;
                    strncpy(req->result, s_session.last_result,
                            sizeof(req->result) - 1);
                    req->result[sizeof(req->result) - 1] = '\0';
                    strncpy(req->data, s_session.last_data,
                            sizeof(req->data) - 1);
                    req->data[sizeof(req->data) - 1] = '\0';
                }

                /* Signal the caller — semaphore ops provide memory barrier. */
                xSemaphoreGive(req->done_sem);

                /* Clean up if caller already gave up. */
                if (req->abandoned) {
                    vSemaphoreDelete(req->done_sem);
                    free(req);
                }
                /* else: caller owns req until it reads + frees. */
            }
        }
    }
}

esp_err_t bt_link_init(uint32_t cmd_timeout_ms)
{
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
    if (err != ESP_OK) return err;
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

    bt_link_session_init(&s_session,
                         cmd_timeout_ms ? cmd_timeout_ms : BT_LINK_DEFAULT_TIMEOUT_MS);
    bt_link_linebuf_init(&s_linebuf);

    s_cmd_queue = xQueueCreate(4, sizeof(bt_link_request_t *));
    if (!s_cmd_queue) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    s_send_mutex = xSemaphoreCreateMutex();
    if (!s_send_mutex) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    if (xTaskCreate(bt_link_task, "bt_link", BT_LINK_TASK_STACK, NULL,
                    BT_LINK_TASK_PRIO, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    ESP_LOGI(TAG, "init: UART1 tx=%d rx=%d @115200, timeout=%ums",
             BT_LINK_UART_TX_GPIO, BT_LINK_UART_RX_GPIO,
             (unsigned)(cmd_timeout_ms ? cmd_timeout_ms : BT_LINK_DEFAULT_TIMEOUT_MS));
    return ESP_OK;

fail:
    if (s_send_mutex) { vSemaphoreDelete(s_send_mutex); s_send_mutex = NULL; }
    if (s_cmd_queue) { vQueueDelete(s_cmd_queue); s_cmd_queue = NULL; }
    if (uart_installed) uart_driver_delete(BT_LINK_UART);
    return err;
}

/* Subscribers are registered before the command stream gets busy; n_subs only
 * grows, so the brief overlap with the task's reads is benign in practice. */
int bt_link_subscribe(bt_link_event_cb cb, void *ctx)
{
    return bt_link_session_subscribe(&s_session, cb, ctx);
}

esp_err_t bt_link_send(const char *cmd, bt_link_cmd_state_t *out_state,
                       char *result, size_t result_sz,
                       char *data, size_t data_sz)
{
    if (!cmd || !s_send_mutex) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_send_mutex, portMAX_DELAY);

    /* Heap-allocate per-request state with its own completion semaphore. */
    bt_link_request_t *req = (bt_link_request_t *)malloc(sizeof(bt_link_request_t));
    if (!req) {
        xSemaphoreGive(s_send_mutex);
        return ESP_ERR_NO_MEM;
    }
    memset(req, 0, sizeof(bt_link_request_t));
    strncpy(req->cmd, cmd, BT_LINK_LINE_MAX - 1);

    /* Create per-request semaphore (initially empty — worker signals it). */
    req->done_sem = xSemaphoreCreateBinary();
    req->abandoned = false;
    if (!req->done_sem) {
        free(req);
        xSemaphoreGive(s_send_mutex);
        return ESP_ERR_NO_MEM;
    }

    /* Queue to worker task. Use finite timeout so queue-full doesn't block forever. */
    if (xQueueSend(s_cmd_queue, &req, pdMS_TO_TICKS(250)) != pdTRUE) {
        /* Queue full — free request resources and return. */
        vSemaphoreDelete(req->done_sem);
        free(req);
        xSemaphoreGive(s_send_mutex);
        return ESP_ERR_TIMEOUT;
    }

    /* Block on this request's own semaphore.
     * Worker signals req->done_sem when the UART response arrives or times out. */
    if (xSemaphoreTake(req->done_sem, pdMS_TO_TICKS(BT_LINK_DEFAULT_TIMEOUT_MS + 500)) != pdTRUE) {
        /* Timed out — mark abandoned so worker cleans up our semaphore + memory. */
        req->abandoned = true;
        xSemaphoreGive(s_send_mutex);
        return ESP_ERR_TIMEOUT;
    }

    /* Worker has written req->state/result/data (synced by semaphore barrier). */
    bt_link_cmd_state_t final_state = req->state;
    esp_err_t ret = ESP_OK;
    if (final_state == BT_LINK_CMD_TIMEOUT) {
        ret = ESP_ERR_TIMEOUT;
    }

    if (out_state) *out_state = req->state;
    if (result && result_sz) {
        strncpy(result, req->result, result_sz - 1);
        result[result_sz - 1] = '\0';
    }
    if (data && data_sz) {
        strncpy(data, req->data, data_sz - 1);
        data[data_sz - 1] = '\0';
    }

    /* Clean up heap-allocated request. */
    vSemaphoreDelete(req->done_sem);
    free(req);
    xSemaphoreGive(s_send_mutex);
    return ret;
}