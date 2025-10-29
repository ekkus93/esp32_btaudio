#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline const uint8_t *esp_bt_dev_get_address(void)
{
    static uint8_t addr[6] = {0};
    return addr;
}

#ifdef __cplusplus
}
#endif
