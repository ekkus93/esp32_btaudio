#include "bt_app_core.h"
#include <stdlib.h>
#include <string.h>

/* Host-side stub implementations used by host/unit tests. These avoid
 * pulling in FreeRTOS headers and provide lightweight behavior suitable
 * for the Unity tests in `test/host_test`.
 */

void bt_app_task_start_up(void)
{
    /* No-op on host tests */
}

int bt_app_work_copy_cb(bt_app_msg_t *msg, void *p_dest, int len)
{
    if (msg == NULL || p_dest == NULL || len < 0) return BT_APP_WORK_FAIL;
    if (msg->param) return BT_APP_WORK_FAIL;

    msg->param = malloc(len);
    if (msg->param) {
        memcpy(msg->param, p_dest, len);
        msg->param_free_cb = bt_app_param_free_cb;
        return BT_APP_WORK_OK;
    }
    return BT_APP_WORK_FAIL;
}

void bt_app_param_free_cb(void *param)
{
    if (param) free(param);
}

bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, void *p_params, int param_len, bt_app_copy_cb_t p_copy_cback)
{
    if (!p_cback) return false;

    bt_app_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.event = event;
    msg.cb = p_cback;

    if (param_len && p_params) {
        bt_app_copy_cb_t copy_cb = p_copy_cback ? p_copy_cback : bt_app_work_copy_cb;
        if (copy_cb(&msg, p_params, param_len) != BT_APP_WORK_OK) {
            return false;
        }
    }

    /* Synchronously call the handler on host tests */
    if (msg.cb) msg.cb(msg.event, msg.param);

    if (msg.param && msg.param_free_cb) msg.param_free_cb(msg.param);
    return true;
}
