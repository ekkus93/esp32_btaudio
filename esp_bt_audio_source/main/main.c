/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"

/* Bluetooth includes */
#include "esp_bt.h"
#include "command_interface.h"
#include "driver/uart.h"
#include "audio_processor.h"
/* Needed for pin fallbacks and i2s port constants when auto-initializing audio */
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "nvs_storage.h"
#include "bt_manager.h"

/* log tags */
#define BT_AV_TAG             "BT_AV"

/* device name */
#define LOCAL_DEVICE_NAME     "ESP_A2DP_SRC"

/* Small FreeRTOS task that polls the command interface */
static void cmd_process_task(void* arg) {
    (void)arg;
    for (;;) {
        cmd_process();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* keepalive output remains silent to avoid audible beeps */

// Stack size settings for better stability
#define BT_APP_TASK_STACK_SIZE    8192     // Increased from default 4096

/*********************************
 * MAIN ENTRY POINT
 ********************************/

void app_main(void)
{
    // Free BLE controller memory to give Classic BT more DRAM
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));    

    /* Very early unbuffered boot marker for programmatic captures. This
     * appears before most initialization and helps external injectors
     * avoid racing the device boot sequence. Use both printf and the
     * ROM-level esp_rom_printf when available. */
    printf("DIAG|BOOT|EARLY_BOOT_MARKER\r\n");
#ifdef CONFIG_IDF_TARGET_ESP32
    esp_rom_printf("DIAG|BOOT|EARLY_BOOT_MARKER\r\n");
#endif

    ESP_LOGI(BT_AV_TAG, "ESP32 Bluetooth Audio Source starting");
    /* Quiet the very chatty audio processor logs so CLI commands aren't
     * drowned out during diagnostics. Raise as needed when deep-diving.
     */
    esp_log_level_set("AUDIO_PROC", ESP_LOG_WARN);
    /* Ensure the console UART driver is installed early so command layer
     * can synchronously read/write without racing with other subsystems.
     * This mirrors the conservative install performed in the command
     * interface but moves it earlier in startup to reduce races. */
#ifdef ESP_PLATFORM
    {
    const int uart_rx_buf = 1024;
    const int uart_tx_buf = 1024;
    /* Resolve the console UART number from config when available; fall
     * back to UART_NUM_0 which is commonly the primary UART on ESP32.
     */
#ifdef CONFIG_ESP_CONSOLE_UART_NUM
    int console_uart = CONFIG_ESP_CONSOLE_UART_NUM;
#else
    int console_uart = UART_NUM_0;
#endif
    /* UART Ownership: main.c installs UART driver once for early diagnostics
     * (unbuffered uart_write_bytes). cmd_init() and other components assume
     * UART is already operational. Do NOT delete the driver after install - 
     * this breaks esp-console, logging, and the cmd layer. Single install only. */
    esp_err_t r = uart_driver_install(console_uart, uart_rx_buf, uart_tx_buf, 0, NULL, 0);
    printf("DIAG|BOOT|EARLY_UART_INSTALL|ret=%d,installed=%d\r\n", (int)r, uart_is_driver_installed(console_uart) ? 1 : 0);
#ifdef CONFIG_IDF_TARGET_ESP32
    esp_rom_printf("DIAG|BOOT|EARLY_UART_INSTALL|ret=%d,installed=%d\r\n", (int)r, uart_is_driver_installed(console_uart) ? 1 : 0);
#endif
    if (uart_is_driver_installed(console_uart)) {
        const char ready[] = "DIAG|BOOT|UART_READY_FOR_CMD_LAYER\r\n";
        uart_write_bytes(console_uart, ready, sizeof(ready)-1);
        /* Ensure the ready string is transmitted before proceeding so host
         * injectors that watch for this marker don't miss it due to buffering. */
        (void)uart_wait_tx_done(console_uart, pdMS_TO_TICKS(50));
    }
    }
#endif
    
    // Initialize NVS flash storage (platform service owned by main.c).
    // This must be called once before any component uses NVS (bt_manager,
    // audio_processor, etc.). nvs_storage_init() handles version mismatch
    // and erase-on-error internally.
    ESP_ERROR_CHECK(nvs_storage_init());
    
    // Initialize and start Bluetooth via bt_manager so the command interface
    // and other components using the manager APIs are ready for SCAN/PAIR.
    bt_manager_init_t bt_cfg = {
        .device_name = LOCAL_DEVICE_NAME,
        .connected_cb = NULL,
        .disconnected_cb = NULL,
    };

    if (bt_manager_init(&bt_cfg) != ESP_OK) {
        ESP_LOGE(BT_AV_TAG, "bt_manager_init failed");
    } else {
        ESP_LOGI(BT_AV_TAG, "Bluetooth manager initialized");
    }

    // Initialize command interface for serial commands and events
    // This will create the UART driver and prepare the command parser.
    // The cmd_process() function must be called regularly; create a
    // small FreeRTOS task to poll for incoming command lines.
#ifdef ESP_PLATFORM
    if (cmd_init() != CMD_SUCCESS) {
        ESP_LOGW(BT_AV_TAG, "cmd_init() failed or already initialized");
    }
    /* Unconditional boot-time diagnostic: print a plain-text marker so
     * non-interactive captures can reliably detect that command
     * initialization has completed and which UART is in use. Use printf
     * to ensure the message appears on the console even if the UART driver
     * isn't fully installed yet. */
    printf("INFO|CMD_IF|BOOT_DIAG|CMD_INIT_CALLED\r\n");
    /* Also emit an unbuffered ROM-level print so host captures see this even if stdio is buffered */
#ifdef CONFIG_IDF_TARGET_ESP32
    esp_rom_printf("INFO|CMD_IF|BOOT_DIAG|CMD_INIT_CALLED\r\n");
#endif

    // Create a tiny task dedicated to processing command input
    xTaskCreate(cmd_process_task, "cmd_proc", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
    /* One-time diagnostic to indicate the command processing task was created
     * and should be running. This helps programmatic captures detect that the
     * task is active even when the UART driver installation may be delayed. */
    printf("INFO|CMD_IF|CMD_TASK_STARTED\r\n");
    /* duplicate as ROM-level immediate output to reduce race with host capture */
#ifdef CONFIG_IDF_TARGET_ESP32
    esp_rom_printf("INFO|CMD_IF|CMD_TASK_STARTED\r\n");
#endif

    /* Runtime diagnostic: print whether the UART driver is installed for the command UART */
    int uart_installed = uart_is_driver_installed(CONFIG_ESP_CONSOLE_UART_NUM);
    printf("DIAG|CMD_IF|UART_DRIVER_INSTALLED|uart=%d|installed=%d\r\n", CONFIG_ESP_CONSOLE_UART_NUM, uart_installed);
#ifdef ESP_PLATFORM
    /* Auto-initialize and start audio/I2S at boot.
     * Behavior: attempt to read persisted I2S pin overrides from NVS and
     * fall back to the component defaults. On success the audio pipeline
     * will be initialized and the I2S RX channel enabled so downstream
     * consumers (A2DP source) can immediately stream live audio.
     *
     * Note: this intentionally changes the previous deferred-init behavior
     * to enable I2S at boot. If you prefer deferred init revert this block
     * to the prior diagnostic-only query.
     */
    {
        audio_config_t aconf = {
            .sample_rate = AUDIO_SAMPLE_RATE_44K,
            .bit_depth = AUDIO_BIT_DEPTH_16,
            .channels = AUDIO_CHANNEL_STEREO,
            .volume = 80,
            .mute = false,
            .i2s_port = I2S_NUM_0,
            /* fallbacks chosen to match internal defaults in audio_processor.c */
            .i2s_bclk_pin = GPIO_NUM_26,
            .i2s_ws_pin = GPIO_NUM_25,
            .i2s_din_pin = GPIO_NUM_22,
            .i2s_dout_pin = GPIO_NUM_NC,
        };

        int bclk = -1, ws = -1, din = -1, dout = -1;
        /* Best-effort: if NVS has stored pins use them */
        if (nvs_storage_get_i2s_pins(&bclk, &ws, &din, &dout) == ESP_OK) {
            if (bclk >= 0) aconf.i2s_bclk_pin = bclk;
            if (ws >= 0) aconf.i2s_ws_pin = ws;
            if (din >= 0) aconf.i2s_din_pin = din;
            if (dout >= 0) aconf.i2s_dout_pin = dout;
        }

        esp_err_t aerr = audio_processor_init(&aconf);
        if (aerr != ESP_OK) {
            printf("DIAG|AUDIO|STATUS|init_failed|err=%s\r\n", esp_err_to_name(aerr));
        } else {
            aerr = audio_processor_start();
            if (aerr != ESP_OK) {
                printf("DIAG|AUDIO|STATUS|start_failed|err=%s\r\n", esp_err_to_name(aerr));
            } else {
                printf("DIAG|AUDIO|STATUS|initialized=1|running=1|volume=%u|mute=%d|rate=%d|bits=%d|ch=%d\r\n",
                       (unsigned)aconf.volume,
                       aconf.mute ? 1 : 0,
                       (int)aconf.sample_rate,
                       (int)aconf.bit_depth,
                       (int)aconf.channels);
            }
        }
    }
#endif
#endif
    
    ESP_LOGI(BT_AV_TAG, "====================================================");
    ESP_LOGI(BT_AV_TAG, "Bluetooth will scan for and connect to audio devices");
    ESP_LOGI(BT_AV_TAG, "Will play a short beep once per minute");
    ESP_LOGI(BT_AV_TAG, "====================================================");
    
    // Main loop - not needed as FreeRTOS tasks are now running
    while (1) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);  // Just a background task
    }
}
