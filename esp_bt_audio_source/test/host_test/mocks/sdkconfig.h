/* Minimal sdkconfig.h replacement for host unit tests
 * Provides default values for CONFIG_* macros referenced by
 * components/components/bt/common/include/bt_user_config.h
 */

#ifndef __TEST_SDKCONFIG_H__
#define __TEST_SDKCONFIG_H__

/* Core/RTOS */
#define CONFIG_FREERTOS_NUMBER_OF_CORES 1

/* Bluetooth stack feature flags (defaults for host tests) */
#define CONFIG_BT_BLE_DYNAMIC_ENV_MEMORY 0
#define CONFIG_BT_STACK_NO_LOG 0
#define CONFIG_BT_CONTROLLER_ENABLED 0
#define CONFIG_BT_BLUEDROID_PINNED_TO_CORE 0
#define CONFIG_BT_BTC_TASK_STACK_SIZE 4096
#define CONFIG_BT_ALARM_MAX_NUM 50

/* Logging */
#define CONFIG_LOG_DEFAULT_LEVEL 3
#define CONFIG_BOOTLOADER_LOG_LEVEL 3
#define CONFIG_BT_LOG_BTC_TRACE_LEVEL 2
#define CONFIG_BT_LOG_OSI_TRACE_LEVEL 2
#define CONFIG_BT_LOG_BLUFI_TRACE_LEVEL 2

/* Blufi / Nimble */
#define CONFIG_BT_BLE_BLUFI_ENABLE 0
#define CONFIG_BT_NIMBLE_BLUFI_ENABLE 0

/* Memory/debug */
#define CONFIG_BT_BLUEDROID_MEM_DEBUG 0
#define CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST 0
#define CONFIG_BT_ABORT_WHEN_ALLOCATION_FAILS 0

/* HCI log */
#define CONFIG_BT_HCI_LOG_DEBUG_EN 0
#define CONFIG_BT_HCI_LOG_DATA_BUFFER_SIZE 5
#define CONFIG_BT_HCI_LOG_ADV_BUFFER_SIZE 5

#endif /* __TEST_SDKCONFIG_H__ */
