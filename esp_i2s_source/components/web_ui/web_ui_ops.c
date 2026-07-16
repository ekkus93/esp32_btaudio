/*
 * web_ui_ops — async operation queue for long-running HTTP operations.
 *
 * Operations like scan, connect, Wi-Fi provisioning, radio stop/start are
 * queued and return 202 Accepted with an operation ID. Clients poll
 * /api/ops/:id/status for progress/completion.
 */
#include "web_ui_internal.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define OPS_MAX 8
#define OPS_ID_FMT "op-%04d"

typedef enum {
    OPS_STATE_PENDING = 0,
    OPS_STATE_RUNNING,
    OPS_STATE_DONE,
    OPS_STATE_FAILED,
} ops_state_t;

typedef struct {
    char id[16];
    char description[64];
    ops_state_t state;
    esp_err_t result;
    char error_msg[128];
    int64_t created_us;
    int64_t completed_us;
} ops_entry_t;

static ops_entry_t s_ops[OPS_MAX];
static SemaphoreHandle_t s_ops_mutex;
static int s_next_id;

void web_ui_ops_init(void)
{
    s_ops_mutex = xSemaphoreCreateMutex();
    s_next_id = 0;
    memset(s_ops, 0, sizeof(s_ops));
}

esp_err_t web_ui_ops_enqueue(const char *description, web_op_id_t *out_id)
{
    esp_err_t err;
    int idx;

    err = xSemaphoreTake(s_ops_mutex, pdMS_TO_TICKS(1000));
    if (err != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    /* Find free slot. */
    for (idx = 0; idx < OPS_MAX; idx++) {
        if (s_ops[idx].id[0] == '\0') break;
    }
    if (idx == OPS_MAX) {
        xSemaphoreGive(s_ops_mutex);
        return ESP_ERR_NO_MEM;
    }

    /* Create operation ID. */
    snprintf(s_ops[idx].id, sizeof(s_ops[idx].id), OPS_ID_FMT, s_next_id++);
    snprintf(s_ops[idx].description, sizeof(s_ops[idx].description), "%s", description);
    s_ops[idx].state = OPS_STATE_PENDING;
    s_ops[idx].result = ESP_OK;
    s_ops[idx].error_msg[0] = '\0';
    s_ops[idx].created_us = esp_timer_get_time();
    s_ops[idx].completed_us = 0;

    if (out_id) {
        snprintf(out_id->id, sizeof(out_id->id), "%s", s_ops[idx].id);
    }

    xSemaphoreGive(s_ops_mutex);
    return ESP_OK;
}

esp_err_t web_ui_ops_complete(const char *op_id, esp_err_t result, const char *error_msg)
{
    int idx;

    if (xSemaphoreTake(s_ops_mutex, pdMS_TO_TICKS(1000)) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    for (idx = 0; idx < OPS_MAX; idx++) {
        if (strcmp(s_ops[idx].id, op_id) == 0) {
            s_ops[idx].state = (result == ESP_OK) ? OPS_STATE_DONE : OPS_STATE_FAILED;
            s_ops[idx].result = result;
            if (error_msg) {
                snprintf(s_ops[idx].error_msg, sizeof(s_ops[idx].error_msg), "%s", error_msg);
            }
            s_ops[idx].completed_us = esp_timer_get_time();
            xSemaphoreGive(s_ops_mutex);
            return ESP_OK;
        }
    }

    xSemaphoreGive(s_ops_mutex);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t web_ui_ops_status(const char *op_id, web_op_status_t *out)
{
    int idx;

    if (xSemaphoreTake(s_ops_mutex, pdMS_TO_TICKS(1000)) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    for (idx = 0; idx < OPS_MAX; idx++) {
        if (strcmp(s_ops[idx].id, op_id) == 0) {
            snprintf(out->id, sizeof(out->id), "%s", s_ops[idx].id);
            snprintf(out->description, sizeof(out->description), "%s", s_ops[idx].description);
            out->state = s_ops[idx].state;
            out->result = s_ops[idx].result;
            if (s_ops[idx].error_msg[0]) {
                snprintf(out->error_msg, sizeof(out->error_msg), "%s", s_ops[idx].error_msg);
            } else {
                out->error_msg[0] = '\0';
            }
            out->created_us = s_ops[idx].created_us;
            out->completed_us = s_ops[idx].completed_us;
            xSemaphoreGive(s_ops_mutex);
            return ESP_OK;
        }
    }

    xSemaphoreGive(s_ops_mutex);
    return ESP_ERR_NOT_FOUND;
}
