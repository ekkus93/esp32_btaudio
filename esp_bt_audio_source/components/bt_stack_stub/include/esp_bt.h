#pragma once

#include "sdkconfig.h"

#include_next "esp_bt.h"

#if !defined(CONFIG_BT_ENABLED) || !(CONFIG_BT_ENABLED)
#ifdef BT_CONTROLLER_INIT_CONFIG_DEFAULT
#undef BT_CONTROLLER_INIT_CONFIG_DEFAULT
#endif
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() ((esp_bt_controller_config_t){0})
#endif
