#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_err.h"
#include "command_interface.h"
#include "bt_source.h"
#include "nvs_storage.h"

static const char *TAG = "TEST_UTILS_ADPT";

#define EVENT_QUEUE_DEPTH 8
#define EVENT_MAX_LEN     512

static char s_event_queue[EVENT_QUEUE_DEPTH][EVENT_MAX_LEN];
static size_t s_event_head;
static size_t s_event_tail;
static size_t s_event_count;
static bool s_command_interface_ready;
static bool s_nvs_ready;

static void event_queue_reset(void)
{
    s_event_head = 0;
    s_event_tail = 0;
    s_event_count = 0;
    for (size_t i = 0; i < EVENT_QUEUE_DEPTH; ++i) {
        s_event_queue[i][0] = '\0';
    }
}

static void ensure_command_interface(void)
{
    if (s_command_interface_ready) {
        return;
    }

    cmd_status_t status = cmd_init();
    if (status != CMD_SUCCESS) {
        ESP_LOGW(TAG, "cmd_init failed (%d); continuing with existing state", status);
    } else {
        s_command_interface_ready = true;
        ESP_LOGI(TAG, "command interface initialised for tests");
    }
}

void test_utils_reset_state(void)
{
    event_queue_reset();
    ensure_command_interface();

    if (!s_nvs_ready) {
        esp_err_t err = nvs_storage_init();
        if (err == ESP_OK) {
            s_nvs_ready = true;
        } else {
            ESP_LOGW(TAG, "nvs_storage_init failed (%s)", esp_err_to_name(err));
        }
    }

    const char default_pin[] = "000000";
    esp_err_t pin_err = bt_set_default_pin(default_pin);
    if (pin_err != ESP_OK) {
        ESP_LOGW(TAG, "bt_set_default_pin failed (%s)", esp_err_to_name(pin_err));
    }

    if (s_nvs_ready) {
        esp_err_t nvs_err = nvs_storage_set_default_pin(default_pin);
        if (nvs_err != ESP_OK) {
            ESP_LOGW(TAG, "nvs_storage_set_default_pin failed (%s)", esp_err_to_name(nvs_err));
        }
    }
}

static void event_queue_push(const char *event)
{
    if (!event || event[0] == '\0') {
        return;
    }

    // Drop oldest entry if full
    if (s_event_count == EVENT_QUEUE_DEPTH) {
        s_event_head = (s_event_head + 1) % EVENT_QUEUE_DEPTH;
        --s_event_count;
    }

    strncpy(s_event_queue[s_event_tail], event, EVENT_MAX_LEN - 1);
    s_event_queue[s_event_tail][EVENT_MAX_LEN - 1] = '\0';
    s_event_tail = (s_event_tail + 1) % EVENT_QUEUE_DEPTH;
    ++s_event_count;

    ESP_LOGI(TAG, "captured event: %s", event);
}

bool test_capture_event(char *out_buf, size_t out_len)
{
    if (!out_buf || out_len == 0 || s_event_count == 0) {
        return false;
    }

    strncpy(out_buf, s_event_queue[s_event_head], out_len - 1);
    out_buf[out_len - 1] = '\0';
    s_event_queue[s_event_head][0] = '\0';

    s_event_head = (s_event_head + 1) % EVENT_QUEUE_DEPTH;
    --s_event_count;

    return true;
}

bool test_send_serial_cmd(const char *cmd)
{
    if (!cmd || cmd[0] == '\0') {
        return false;
    }

    ensure_command_interface();

    cmd_context_t ctx;
    cmd_status_t status = cmd_parse(cmd, &ctx);
    if (status != CMD_SUCCESS) {
        ESP_LOGW(TAG, "cmd_parse failed (%d) for line: %s", status, cmd);
        return false;
    }

    status = cmd_execute(&ctx);
    if (status != CMD_SUCCESS) {
        ESP_LOGW(TAG, "cmd_execute failed (%d) for command type %d", status, ctx.type);
    }

    return status == CMD_SUCCESS;
}

void test_push_event(const char *ev)
{
    event_queue_push(ev);
}
