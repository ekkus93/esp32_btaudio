/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* Signal for BT app task */
enum {
    BT_APP_SIG_WORK_DISPATCH = 0,
};

/* BT app task handler */
typedef void (* bt_app_cb_t)(uint16_t event, void *param);

/* BT app work status */
enum {
    BT_APP_WORK_OK = 0,
    BT_APP_WORK_FAIL,
};

/* BT app message */
typedef struct {
    uint16_t sig;      // Signal
    uint16_t event;    // Event
    bt_app_cb_t cb;    // Callback
    void *param;       // Parameter
    void (*param_free_cb)(void *param);  // Parameter free callback
} bt_app_msg_t;

/* Function declaration */
typedef int (* bt_app_copy_cb_t)(bt_app_msg_t *msg, void *p_dest, int len);

/**
 * @brief     Task startup function
 */
void bt_app_task_start_up(void);

/**
 * @brief     Task shutdown function
 */
void bt_app_task_shut_down(void);

/**
 * @brief     Callback function for parameter deep copy
 */
int bt_app_work_copy_cb(bt_app_msg_t *msg, void *p_dest, int len);

/**
 * @brief     Callback function for parameter free
 */
void bt_app_param_free_cb(void *param);

/**
 * @brief     Work dispatcher
 */
bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, void *p_params, int param_len, bt_app_copy_cb_t p_copy_cback);
