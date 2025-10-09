#include <stdio.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_bt.h"

// Simple storage for last call parameters for assertions
static char last_mac[18] = {0};
static int last_pin_len = 0;
static char last_pin[32] = {0};
static int last_confirm = -1;

void mock_gap_reset(void) {
    last_mac[0] = '\0';
    last_pin_len = 0;
    last_pin[0] = '\0';
    last_confirm = -1;
}

const char* mock_gap_get_last_mac(void) { return last_mac; }
int mock_gap_get_last_pin_len(void) { return last_pin_len; }
const char* mock_gap_get_last_pin(void) { return last_pin; }
int mock_gap_get_last_confirm(void) { return last_confirm; }

// Mock implementations
int esp_bt_gap_pin_reply(uint8_t *bd_addr, bool accept, uint8_t pin_code_len, uint8_t *pin_code) {
    if (bd_addr) {
        sprintf(last_mac, "%02x:%02x:%02x:%02x:%02x:%02x",
                bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5]);
    } else {
        last_mac[0] = '\0';
    }
    if (pin_code && pin_code_len > 0) {
        size_t cpy = pin_code_len < sizeof(last_pin)-1 ? pin_code_len : sizeof(last_pin)-1;
        memcpy(last_pin, pin_code, cpy);
        last_pin[cpy] = '\0';
        last_pin_len = (int)cpy;
    } else {
        last_pin[0] = '\0';
        last_pin_len = 0;
    }
    (void)accept; // silence unused in-host tests
    return 0; // success
}

int esp_bt_gap_ssp_confirm_reply(uint8_t *bd_addr, bool accept) {
    if (bd_addr) {
        sprintf(last_mac, "%02x:%02x:%02x:%02x:%02x:%02x",
                bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5]);
    } else {
        last_mac[0] = '\0';
    }
    last_confirm = accept ? 1 : 0;
    return 0;
}