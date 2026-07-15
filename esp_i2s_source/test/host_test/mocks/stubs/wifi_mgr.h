/* Stub wifi_mgr.h for host tests */
#ifndef STUB_WIFI_MGR_H
#define STUB_WIFI_MGR_H

#include "esp_err.h"
#include <stdbool.h>

#define WIFI_MGR_SSID_MAX  32
#define WIFI_MGR_PASS_MAX  64

typedef struct {
    char mode[4];
    char state[12];
    char ssid[WIFI_MGR_SSID_MAX + 1];
    char ip[16];
    int  rssi;
    bool ap_on;
    char ap_ssid[WIFI_MGR_SSID_MAX + 1];
    bool ap_secured;
    char ap_ip[16];
    int  ap_clients;
} wifi_mgr_info_t;

esp_err_t wifi_mgr_init(void);
esp_err_t wifi_mgr_set_creds(const char *ssid, const char *pass);
esp_err_t wifi_mgr_reset(void);
void wifi_mgr_get_info(wifi_mgr_info_t *out);
bool wifi_mgr_ap_enabled(void);

#endif
