/* Stub wifi_mgr.h for host tests */
#ifndef STUB_WIFI_MGR_H
#define STUB_WIFI_MGR_H

#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    char mode[16];
    char state[16];
    char ssid[32];
    char ip[16];
    int    rssi;
    bool   ap_on;
    char   ap_ssid[32];
    char   ap_pass[64];
    char   ap_ip[16];
    int    ap_clients;
} wifi_mgr_info_t;

esp_err_t wifi_mgr_init(void);
void wifi_mgr_get_info(wifi_mgr_info_t *out);
bool wifi_mgr_ap_enabled(void);

#endif
