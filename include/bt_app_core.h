#ifndef __BT_APP_CORE_H__
#define __BT_APP_CORE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define BT_APP_CORE_TASK_NAME            "BtAppT"
#define BT_APP_CORE_TASK_STACK_SIZE      3072
#define BT_APP_CORE_TASK_PRIO            configMAX_PRIORITIES - 3
#define BT_APP_CORE_TASK_QUEUE_SIZE      20

typedef struct {
    uint16_t             sig;      /*!< signal to bt_app_task */
    void                 *param;   /*!< parameter to signal */
} bt_app_msg_t;

typedef void (* bt_app_cb_t) (uint16_t event, void *param);
typedef void (* bt_app_copy_cb_t) (void *param); // Add this line

typedef struct {
    bt_app_cb_t cb;
    uint16_t event;
    void *param;
    int param_len;
    bt_app_copy_cb_t copy_cb;
} bt_app_work_item_t;

bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, void *p_params, int param_len, bt_app_copy_cb_t p_copy_cback);
void bt_app_task_start_up(void);
void bt_app_task_shut_down(void);

#endif /* __BT_APP_CORE_H__ */
