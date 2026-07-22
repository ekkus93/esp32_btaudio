/* console — USB-Serial-JTAG line reader + command dispatch (WIFI-1c). */
#include "console.h"
#include "wifi_mgr.h"
#include "web_ui.h"
#include "stations.h"
#include "runtime_capabilities.h"

#include <string.h>
#include <strings.h>
#include <stdio.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CONSOLE_LINE_MAX 160

/* WIFI <ssid> [pass] | WIFI STATUS | WIFI RESET */
static void handle_wifi(char *args)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        printf("ERR|WIFI|MISSING_ARG|usage: WIFI <ssid> [pass] | STATUS | RESET\n");
        return;
    }
    if (strcasecmp(args, "STATUS") == 0) {
        char buf[160];
        wifi_mgr_get_status(buf, sizeof(buf));
        printf("OK|WIFI|STATUS|%s\n", buf);
        return;
    }
    if (strcasecmp(args, "RESET") == 0) {
        esp_err_t e = wifi_mgr_reset();
        if (e == ESP_OK) {
            printf("OK|WIFI|RESET|AP\n");
        } else {
            printf("ERR|WIFI|RESET|%s\n", esp_err_to_name(e));
        }
        return;
    }
    /* WIFI <ssid> [pass] — ssid is the first token, pass is the rest (may be
     * empty for an open network). WPA2 passwords have no spaces in practice. */
    char *ssid = args;
    char *pass = strchr(args, ' ');
    if (pass) {
        *pass++ = '\0';
        while (*pass == ' ') pass++;
    } else {
        pass = "";
    }
    esp_err_t e = wifi_mgr_set_creds(ssid, pass);
    if (e == ESP_OK) {
        printf("OK|WIFI|PROVISIONED|ssid=%s\n", ssid);
    } else {
        printf("ERR|WIFI|SET_FAILED|%s\n", esp_err_to_name(e));
    }
}

/* AUTH ROTATE — the only supported subcommand (FIX3 §5.5). Local
 * physical-presence path only: never forwarded to the WROOM32, never
 * reachable over HTTP. */
static void handle_auth(char *args)
{
    while (*args == ' ') args++;
    if (strcasecmp(args, "ROTATE") != 0) {
        printf("ERR|AUTH|UNKNOWN_SUBCOMMAND|usage: AUTH ROTATE\n");
        return;
    }
    esp_err_t e = web_ui_auth_rotate();
    if (e != ESP_OK) {
        printf("ERR|AUTH|ROTATE_FAILED|%s\n", esp_err_to_name(e));
    }
    /* On success, web_ui_auth_rotate() itself prints AUTH|BOOTSTRAP_TOKEN|
     * and AUTH|TOKEN_ROTATED — no further output here. */
}

/* STATIONS RESET — the only supported subcommand. Recovery path for a
 * CRC-corrupt persisted blob that stations_init() deliberately refused to
 * auto-repair (FIX3 §8.3). Local physical-presence path only: never
 * forwarded to the WROOM32, never reachable over HTTP — same trust
 * boundary as AUTH ROTATE. Republishes runtime_capabilities so
 * capabilities.stations flips true immediately, no reboot required. */
static void handle_stations(char *args)
{
    while (*args == ' ') args++;
    if (strcasecmp(args, "RESET") != 0) {
        printf("ERR|STATIONS|UNKNOWN_SUBCOMMAND|usage: STATIONS RESET\n");
        return;
    }
    esp_err_t e = stations_reset_persisted();
    if (e == ESP_OK) {
        runtime_capabilities_t caps;
        runtime_capabilities_get(&caps);
        caps.stations = true;
        runtime_capabilities_publish(&caps);
        printf("OK|STATIONS|RESET|count=%d\n", stations_count());
    } else {
        printf("ERR|STATIONS|RESET_FAILED|%s\n", esp_err_to_name(e));
    }
}

static void dispatch(char *line)
{
    while (*line == ' ') line++;
    if (*line == '\0') return;
    char *args = strchr(line, ' ');
    if (args) *args++ = '\0';
    else args = line + strlen(line);   /* empty args */

    if (strcasecmp(line, "WIFI") == 0) {
        handle_wifi(args);
    } else if (strcasecmp(line, "AUTH") == 0) {
        handle_auth(args);
    } else if (strcasecmp(line, "STATIONS") == 0) {
        handle_stations(args);
    } else if (strcasecmp(line, "STATUS") == 0) {
        char buf[160];
        wifi_mgr_get_status(buf, sizeof(buf));
        printf("OK|STATUS|%s\n", buf);
    } else {
        printf("ERR|UNKNOWN|%s\n", line);
    }
    fflush(stdout);
}

static void console_task(void *arg)
{
    (void)arg;
    uint8_t rx[64];
    char line[CONSOLE_LINE_MAX];
    size_t len = 0;
    printf("DIAG|CONSOLE|READY|cmds=WIFI <ssid> [pass],WIFI STATUS,WIFI RESET,AUTH ROTATE,STATIONS RESET,STATUS\n");
    fflush(stdout);
    for (;;) {
        int n = usb_serial_jtag_read_bytes(rx, sizeof(rx), pdMS_TO_TICKS(100));
        for (int i = 0; i < n; i++) {
            char c = (char)rx[i];
            if (c == '\r' || c == '\n') {
                if (len > 0) {
                    line[len] = '\0';
                    dispatch(line);
                    len = 0;
                }
            } else if (len < CONSOLE_LINE_MAX - 1) {
                line[len++] = c;
            }
        }
    }
}

esp_err_t console_start(void)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) return err;
    /* Route stdio through the driver so printf() stays consistent with reads. */
    usb_serial_jtag_vfs_use_driver();
    if (xTaskCreate(console_task, "console", 4096, NULL, tskIDLE_PRIORITY + 2, NULL)
            != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
