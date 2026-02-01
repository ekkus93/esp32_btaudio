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

/**
 * @brief Load audio configuration for boot-time initialization
 * 
 * Centralizes all audio policy decisions (pin assignments, sample rate,
 * volume, etc.) in one place. Uses Kconfig compile-time defaults which
 * can be overridden via menuconfig, and checks NVS for persisted pin
 * overrides that take precedence at runtime.
 * 
 * @return audio_config_t Fully-populated audio configuration ready for
 *                        audio_processor_init()
 * 
 * Configuration hierarchy (highest precedence first):
 * 1. NVS runtime overrides (I2S pins only for now)
 * 2. Kconfig compile-time defaults (sample rate, volume, bit depth, autostart)
 * 
 * Benefits:
 * - Single source of truth for audio boot policy
 * - Configurable via menuconfig (idf.py menuconfig)
 * - Runtime NVS overrides for field customization
 * - Separates "what" (policy) from "how" (initialization)
 */
static audio_config_t load_audio_boot_config(void)
{
    audio_config_t aconf = {
        /* Use Kconfig defaults - configurable via menuconfig */
        .sample_rate = (CONFIG_AUDIO_DEFAULT_SAMPLE_RATE == 44100) ? AUDIO_SAMPLE_RATE_44K :
                       (CONFIG_AUDIO_DEFAULT_SAMPLE_RATE == 48000) ? AUDIO_SAMPLE_RATE_48K :
                       (CONFIG_AUDIO_DEFAULT_SAMPLE_RATE == 32000) ? AUDIO_SAMPLE_RATE_32K :
                       (CONFIG_AUDIO_DEFAULT_SAMPLE_RATE == 22050) ? AUDIO_SAMPLE_RATE_22K :
                       (CONFIG_AUDIO_DEFAULT_SAMPLE_RATE == 16000) ? AUDIO_SAMPLE_RATE_16K :
                       AUDIO_SAMPLE_RATE_44K, /* fallback if custom value */
        .bit_depth = (CONFIG_AUDIO_DEFAULT_BIT_DEPTH == 16) ? AUDIO_BIT_DEPTH_16 :
                     (CONFIG_AUDIO_DEFAULT_BIT_DEPTH == 24) ? AUDIO_BIT_DEPTH_24 :
                     (CONFIG_AUDIO_DEFAULT_BIT_DEPTH == 32) ? AUDIO_BIT_DEPTH_32 :
                     AUDIO_BIT_DEPTH_16, /* fallback */
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = CONFIG_AUDIO_DEFAULT_VOLUME,
        .mute = false,
        .i2s_port = I2S_NUM_0,
        /* Fallback pin assignments match internal defaults in audio_processor.c
         * to ensure consistent behavior across boot paths */
        .i2s_bclk_pin = GPIO_NUM_26,
        .i2s_ws_pin = GPIO_NUM_25,
        .i2s_din_pin = GPIO_NUM_22,
        .i2s_dout_pin = GPIO_NUM_NC,
    };

    /* Best-effort NVS override: if user has stored custom I2S pins, use them.
     * This allows runtime pin configuration without recompiling. */
    int bclk = -1, ws = -1, din = -1, dout = -1;
    if (nvs_storage_get_i2s_pins(&bclk, &ws, &din, &dout) == ESP_OK) {
        if (bclk >= 0) aconf.i2s_bclk_pin = bclk;
        if (ws >= 0) aconf.i2s_ws_pin = ws;
        if (din >= 0) aconf.i2s_din_pin = din;
        if (dout >= 0) aconf.i2s_dout_pin = dout;
    }

    return aconf;
}

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

    /* ========== UART Driver Initialization (Early Boot) ==========
     * 
     * OWNERSHIP: main.c owns UART driver installation for early diagnostics.
     * cmd_init() and all other components assume UART is already operational.
     * 
     * RATIONALE: Early boot diagnostics require unbuffered uart_write_bytes()
     * before subsystems initialize. printf/esp_rom_printf are insufficient
     * (buffered/unreliable for programmatic test harness captures).
     * 
     * CONTRACT: Single install only - NEVER call uart_driver_delete() after
     * install as it breaks esp-console, logging, and the cmd layer.
     */
#ifdef ESP_PLATFORM
    const int uart_rx_buf = 1024;
    const int uart_tx_buf = 1024;
    
#ifdef CONFIG_ESP_CONSOLE_UART_NUM
    const int console_uart = CONFIG_ESP_CONSOLE_UART_NUM;
#else
    const int console_uart = UART_NUM_0;
#endif

    esp_err_t r = uart_driver_install(console_uart, uart_rx_buf, uart_tx_buf, 0, NULL, 0);
    printf("DIAG|BOOT|EARLY_UART_INSTALL|ret=%d,installed=%d\r\n", 
           (int)r, uart_is_driver_installed(console_uart) ? 1 : 0);
#ifdef CONFIG_IDF_TARGET_ESP32
    esp_rom_printf("DIAG|BOOT|EARLY_UART_INSTALL|ret=%d,installed=%d\r\n", 
                   (int)r, uart_is_driver_installed(console_uart) ? 1 : 0);
#endif

    /* Emit critical marker for test harness: UART driver is ready, cmd layer
     * can now perform synchronous I/O without driver installation races. */
    if (uart_is_driver_installed(console_uart)) {
        const char ready[] = "DIAG|BOOT|UART_READY_FOR_CMD_LAYER\r\n";
        uart_write_bytes(console_uart, ready, sizeof(ready)-1);
        uart_wait_tx_done(console_uart, pdMS_TO_TICKS(50));
    }
#endif
    
    /* ========== Platform Services Initialization ==========
     * Initialize NVS flash storage (platform service owned by main.c).
     * This must be called once before any component uses NVS (bt_manager,
     * audio_processor, etc.). nvs_storage_init() handles version mismatch
     * and erase-on-error internally.
     */
    ESP_ERROR_CHECK(nvs_storage_init());
    
    /* ========== Command Interface Initialization ==========
     * Initialize command interface BEFORE subsystems (BT, Audio) so that
     * commands are available immediately when subsystems become ready.
     * This allows SCAN/PAIR/PLAY commands to work as soon as BT initializes.
     * 
     * RATIONALE: Command interface should be the "control plane" that's
     * ready before the "data plane" (BT, Audio) initializes. This prevents
     * the confusing situation where BT is ready but commands aren't yet
     * available to control it.
     */
#ifdef ESP_PLATFORM
    if (cmd_init() != CMD_SUCCESS) {
        ESP_LOGW(BT_AV_TAG, "cmd_init() failed or already initialized");
    }
    printf("INFO|CMD_IF|BOOT_DIAG|CMD_INIT_CALLED\r\n");
#ifdef CONFIG_IDF_TARGET_ESP32
    esp_rom_printf("INFO|CMD_IF|BOOT_DIAG|CMD_INIT_CALLED\r\n");
#endif

    // Create command processing task - this starts the command interface
    // event loop so commands can be processed as soon as they arrive.
    xTaskCreate(cmd_process_task, "cmd_proc", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
    printf("INFO|CMD_IF|CMD_TASK_STARTED\r\n");
#ifdef CONFIG_IDF_TARGET_ESP32
    esp_rom_printf("INFO|CMD_IF|CMD_TASK_STARTED\r\n");
#endif
#endif

    /* ========== Bluetooth Initialization ==========
     * Initialize BT manager now that command interface is ready.
     * Users can immediately issue SCAN/PAIR commands via the command interface.
     */
    bt_manager_init_t bt_cfg = {
        .device_name = LOCAL_DEVICE_NAME,
        .connected_cb = NULL,
        .disconnected_cb = NULL,
    };

    if (bt_manager_init(&bt_cfg) != ESP_OK) {
        ESP_LOGE(BT_AV_TAG, "bt_manager_init failed");
    } else {
        ESP_LOGI(BT_AV_TAG, "Bluetooth manager initialized - SCAN/PAIR commands ready");
    }

    /* ========== Audio Initialization ==========
     * Initialize and start audio/I2S at boot if autostart is enabled.
     * Autostart can be disabled via NVS (audio_autostart=0) to defer audio
     * init until explicitly commanded. This allows systems that don't need
     * audio at boot to save resources or control initialization timing.
     * 
     * Configuration hierarchy (highest precedence first):
     * 1. NVS runtime setting (if configured)
     * 2. Kconfig compile-time default (CONFIG_AUDIO_AUTOSTART_DEFAULT)
     * 
     * The load_audio_boot_config() function encapsulates all audio policy
     * decisions (pins, sample rate, volume) and NVS override logic.
     */
#ifdef ESP_PLATFORM
    {
        uint8_t autostart = CONFIG_AUDIO_AUTOSTART_DEFAULT ? 1 : 0; /* Kconfig default */
        esp_err_t autostart_err = nvs_storage_get_audio_autostart(&autostart);
        if (autostart_err == ESP_ERR_NOT_FOUND) {
            /* Not set in NVS, use Kconfig default */
            autostart = CONFIG_AUDIO_AUTOSTART_DEFAULT ? 1 : 0;
        }

        if (autostart) {
            audio_config_t aconf = load_audio_boot_config();

            esp_err_t aerr = audio_processor_init(&aconf);
            if (aerr != ESP_OK) {
                printf("DIAG|AUDIO|STATUS|init_failed|err=%s\r\n", esp_err_to_name(aerr));
            } else {
                aerr = audio_processor_start();
                if (aerr != ESP_OK) {
                    printf("DIAG|AUDIO|STATUS|start_failed|err=%s\r\n", esp_err_to_name(aerr));
                } else {
                    printf("DIAG|AUDIO|STATUS|initialized=1|running=1|autostart=1|volume=%u|mute=%d|rate=%d|bits=%d|ch=%d\r\n",
                           (unsigned)aconf.volume,
                           aconf.mute ? 1 : 0,
                           (int)aconf.sample_rate,
                           (int)aconf.bit_depth,
                           (int)aconf.channels);
                }
            }
        } else {
            printf("DIAG|AUDIO|STATUS|autostart=0|deferred=1\r\n");
            ESP_LOGI(BT_AV_TAG, "Audio autostart disabled - audio initialization deferred");
        }
    }
#endif
    
    ESP_LOGI(BT_AV_TAG, "====================================================");
    ESP_LOGI(BT_AV_TAG, "Bluetooth will scan for and connect to audio devices");
    ESP_LOGI(BT_AV_TAG, "Will play a short beep once per minute");
    ESP_LOGI(BT_AV_TAG, "====================================================");
    
    // app_main returns; FreeRTOS scheduler keeps running
}
