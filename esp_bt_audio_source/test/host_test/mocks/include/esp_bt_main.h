#pragma once

#ifdef __cplusplus
extern "C" {
#endif

static inline int esp_bt_controller_mem_release(int mode)
{
    (void)mode;
    return 0;
}

#ifdef __cplusplus
}
#endif
