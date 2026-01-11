#ifndef COMMANDS_PRIV_H
#define COMMANDS_PRIV_H

#include "command_interface.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_processor.h"
#include "bt_manager.h"
#include "nvs_storage.h"
#include "esp_err.h"

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "esp_partition.h"
#else
#include "esp_bt.h"
#include "esp_log.h"
#include "mock_uart.h"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#endif

#ifdef ESP_PLATFORM
/* Prefer console UART when available to avoid driver-not-installed errors. */
#ifdef CONFIG_ESP_CONSOLE_UART_NUM
#define CMD_UART_NUM CONFIG_ESP_CONSOLE_UART_NUM
#else
#define CMD_UART_NUM UART_NUM_1
#endif
#else
#ifndef CMD_UART_NUM
#define CMD_UART_NUM UART_NUM_1
#endif
#endif

#define CMD_FILES_WARN_NAME_MAX 80
#define CMD_FILES_ITEM_NAME_MAX 128
#define CMD_FILES_SUMMARY_ROOT_MAX 120
#define CMD_BEEP_DURATION_MS 10000U
#define CMD_BEEP_FREQ_HZ 261.63

#if !defined(ESP_PLATFORM)
extern int g_mock_log_level;
#endif

/* Lightweight wrappers to keep clang-analyzer from flagging stdlib calls as insecure. */
int cmd_vsnprintf_safe(char *dst, size_t dst_size, const char *fmt, va_list args) __attribute__((format(printf, 3, 0)));
int cmd_snprintf_safe(char *dst, size_t dst_size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#define snprintf(...) cmd_snprintf_safe(__VA_ARGS__)

void *cmd_memcpy_safe(void *dst, const void *src, size_t len);
#define memcpy(...) cmd_memcpy_safe(__VA_ARGS__)
void *cmd_memmove_safe(void *dst, const void *src, size_t len);
#define memmove(...) cmd_memmove_safe(__VA_ARGS__)
void *cmd_memset_safe(void *dst, int value, size_t len);
#define memset(...) cmd_memset_safe(__VA_ARGS__)

void cmd_safe_copy(char *dst, size_t dst_size, const char *src);
void cmd_safe_append(char *dst, size_t dst_size, const char *suffix);
void copy_truncated_identifier(const char *src, char *dst, size_t dst_size);
bool cmd_parse_log_level(const char *level_str, int *out_level);

uint64_t cmd_get_timestamp_ms(void);
void cmd_append_metadata(char *buf, size_t buf_len, const char *key, const char *value);

const char *cmd_files_get_root(void);
esp_err_t cmd_mount_spiffs_if_needed(void);

#if defined(UNIT_TEST)
typedef void (*cmd_test_spiffs_mount_hook_t)(void);
void cmd_test_install_spiffs_mount_hook(cmd_test_spiffs_mount_hook_t hook);
void cmd_test_notify_spiffs_mount_hook(void);
#else
static inline void cmd_test_notify_spiffs_mount_hook(void) {}
#endif

#endif /* COMMANDS_PRIV_H */
