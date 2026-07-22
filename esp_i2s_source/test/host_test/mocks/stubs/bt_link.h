/* Stub bt_link.h for host tests */
#ifndef STUB_BT_LINK_H
#define STUB_BT_LINK_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#define BT_LINK_DEFAULT_TIMEOUT_MS 2000
#define BT_LINK_FIELD_MAX 64

typedef enum {
    BT_LINK_CMD_DONE_OK = 0,
    BT_LINK_CMD_DONE_ERR,
    BT_LINK_CMD_TIMEOUT,
} bt_link_cmd_state_t;

/* Stub — not used in ctrl_init() host tests */
esp_err_t bt_link_init(uint32_t timeout_ms);
esp_err_t bt_link_send(const char *cmd, bt_link_cmd_state_t *st, char *result, size_t result_sz, char *data, size_t data_sz);

#endif
