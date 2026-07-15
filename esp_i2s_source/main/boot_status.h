/*
 * boot_status_t — per-component result of run_boot_sequence() (SPEC §5.2).
 * Split into its own header so test_main_boot.c can link against the real
 * run_boot_sequence() in main.c without pulling in app_main()'s infinite
 * diagnostics loop.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool i2s_ok;
    bool bt_link_ok;
    bool radio_ok;
    bool stations_ok;
    bool ctrl_ok;
    bool audio_task_ok;
    bool wifi_ok;
    bool console_ok;
    bool web_ok;
    bool ctrl_start_ok;
} boot_status_t;

/* Runs boot steps 1-15 (NVS through the async WROOM probe kickoff) exactly
 * once and returns the aggregated result. Does not start the low-rate
 * diagnostics loop or clock_diag — app_main() does that itself after this
 * returns, so it stays out of this testable unit. */
boot_status_t run_boot_sequence(void);

#ifdef __cplusplus
}
#endif
