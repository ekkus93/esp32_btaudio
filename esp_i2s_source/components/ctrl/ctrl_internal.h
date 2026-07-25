/*
 * ctrl_internal.h — private declarations de-static'd from ctrl.c for I2S-3
 * host tests (docs/UNIT_TESTS2_TODO.md). Not part of the public API (that's
 * ctrl.h); nothing outside ctrl.c and its tests should include this.
 *
 * Same de-static-for-testability convention already used in this codebase
 * for radio_internal.h and (esp_bt_audio_source's) audio_processor_internal.h:
 * single definition stays in ctrl.c, extern/non-static declaration lives here.
 */
#pragma once

#include "ctrl.h"
#include "ctrl_sm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Orchestrator FSM instance (defined in ctrl.c). Tests must call
 * ctrl_sm_init(&s_sm, ...) before exercising do_action(), same as
 * orchestrator_task() does. */
extern ctrl_sm_t s_sm;

bool wifi_connected(void);
bool status_running(const char *data);

typedef enum {
    CTRL_RESUME_OK = 0,
    CTRL_RESUME_VOLUME_FAILED,
    CTRL_RESUME_NO_STATION,
    CTRL_RESUME_STATION_NOT_FOUND,
    CTRL_RESUME_PLAY_ENQUEUE_FAILED,
} ctrl_resume_result_t;
const char *resume_result_str(ctrl_resume_result_t r);

typedef enum {
    CTRL_SCAN_OK = 0,
    CTRL_SCAN_RADIO_STOP_FAILED,
    CTRL_SCAN_DISCONNECT_FAILED,
    CTRL_SCAN_COMMAND_FAILED,
    CTRL_SCAN_RECONNECT_FAILED,
    CTRL_SCAN_VOLUME_FAILED,
    CTRL_SCAN_RADIO_RESUME_FAILED,
} ctrl_scan_result_t;
const char *scan_result_str(ctrl_scan_result_t r);

ctrl_action_t do_action(ctrl_action_t act, const ctrl_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
