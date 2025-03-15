#ifndef __BT_APP_INIT_H__
#define __BT_APP_INIT_H__

#include "esp_err.h"

esp_err_t bluetooth_init(void);

// Function to restart the Bluetooth stack
esp_err_t restart_bluetooth_stack(void);

#endif /* __BT_APP_CORE_H__ */

