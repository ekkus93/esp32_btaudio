/*
 * clock_diag — low-priority PCNT-based BCLK/WS frequency meter (MAIN-007/008).
 * Split out of main.c so the boot sequence stays free of PCNT/esp_timer
 * dependencies and is host-testable without pulling in that hardware API.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Spawn the clock diagnostics task. Never blocks; safe to call after boot. */
void clock_diag_start(void);

#ifdef __cplusplus
}
#endif
