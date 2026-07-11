/* console — USB-Serial-JTAG line reader + command dispatch (WIFI-1c). */
#include "console.h"
#include "wifi_mgr.h"

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
        wifi_mgr_reset();
        printf("OK|WIFI|RESET|AP\n");
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

static void dispatch(char *line)
{
    while (*line == ' ') line++;
    if (*line == '\0') return;
    char *args = strchr(line, ' ');
    if (args) *args++ = '\0';
    else args = line + strlen(line);   /* empty args */

    if (strcasecmp(line, "WIFI") == 0) {
        handle_wifi(args);
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
    printf("DIAG|CONSOLE|READY|cmds=WIFI <ssid> [pass],WIFI STATUS,WIFI RESET,STATUS\n");
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
