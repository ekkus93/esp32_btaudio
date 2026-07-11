/*
 * console — minimal S3-local command reader over the USB-Serial-JTAG console
 * (WIFI-1c). Reads lines and dispatches provisioning/diagnostic commands as a
 * fallback when the web UI is unreachable. WIFI-1c ships the WIFI + STATUS
 * verbs; TONE/RADIO/BT are added by their respective tasks.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Install the USB-Serial-JTAG driver and spawn the console reader task. */
esp_err_t console_start(void);

#ifdef __cplusplus
}
#endif
