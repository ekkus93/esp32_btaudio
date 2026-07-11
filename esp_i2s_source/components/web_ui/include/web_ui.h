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

#ifdef __cplusplus
}
#endif
