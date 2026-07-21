/*
 * web_ui — esp_http_server serving the embedded (gzipped, single-file) React
 * SPA plus the device REST/WS API (SPEC §5.2). WEB-1a: GET / (the app) and
 * GET /api/status. Provisioning (/api/wifi), the WebSocket, tone, etc. land in
 * WEB-1b/1c/1d.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the HTTP server. Call after wifi_mgr_init() and bt_link_init(). */
esp_err_t web_ui_start(void);

/* Stop the HTTP server (cleanup on startup failure or shutdown). */
esp_err_t web_ui_stop(void);

/* Rotate the bearer auth token (FIX3 §5.5): generates and persists a new
 * token before publishing it, invalidating the old token immediately on
 * success. Intended to be called only from a local, physical-presence path
 * (the USB console's `AUTH ROTATE` command) — never expose this over an
 * unauthenticated network route. Prints AUTH|BOOTSTRAP_TOKEN|<token> then
 * AUTH|TOKEN_ROTATED on success; on failure the old token remains active
 * and nothing is printed. */
esp_err_t web_ui_auth_rotate(void);

#ifdef __cplusplus
}
#endif
