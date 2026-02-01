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
/* Helper to map Kconfig sample rate to enum without nested conditionals */
static audio_sample_rate_t map_sample_rate_from_kconfig(void)
{
    switch (CONFIG_AUDIO_DEFAULT_SAMPLE_RATE) {
        case 44100: return AUDIO_SAMPLE_RATE_44K;
        case 48000: return AUDIO_SAMPLE_RATE_48K;
        case 32000: return AUDIO_SAMPLE_RATE_32K;
        case 22050: return AUDIO_SAMPLE_RATE_22K;
        case 16000: return AUDIO_SAMPLE_RATE_16K;
        default:    return AUDIO_SAMPLE_RATE_44K;  /* fallback if custom value */
    }
}

/* Helper to map Kconfig bit depth to enum without nested conditionals */
static audio_bit_depth_t map_bit_depth_from_kconfig(void)
{
    switch (CONFIG_AUDIO_DEFAULT_BIT_DEPTH) {
        case 16: return AUDIO_BIT_DEPTH_16;
        case 24: return AUDIO_BIT_DEPTH_24;
        case 32: return AUDIO_BIT_DEPTH_32;
        default: return AUDIO_BIT_DEPTH_16;  /* fallback */
    }
}

static audio_config_t load_audio_boot_config(void)
{
    audio_config_t aconf = {
        /* Use Kconfig defaults - configurable via menuconfig */
        .sample_rate = map_sample_rate_from_kconfig(),
        .bit_depth = map_bit_depth_from_kconfig(),
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
    int bclk = -1;
    int word_select = -1;
    int din = -1;
    int dout = -1;
    if (nvs_storage_get_i2s_pins(&bclk, &word_select, &din, &dout) == ESP_OK) {
        if (bclk >= 0) {
            aconf.i2s_bclk_pin = bclk;
        }
        if (word_select >= 0) {
            aconf.i2s_ws_pin = word_select;
        }
        if (din >= 0) {
            aconf.i2s_din_pin = din;
        }
        if (dout >= 0) {
            aconf.i2s_dout_pin = dout;
        }
    }

    return aconf;
}

/*********************************
 * MAIN ENTRY POINT
 * 
 * Error Handling Policy:
 * - Platform services (NVS, UART, BLE mem release): ESP_ERROR_CHECK (fail-fast)
 *   Rationale: System cannot function without these. Failing fast prevents
 *   confusing "half-working" states and makes issues immediately visible.
 *   UART is foundational - cmd interface and all diagnostics depend on it.
 * 
 * - Subsystems (BT, Audio, CMD): Log errors but continue (graceful degradation)
 *   Rationale: Partial functionality > completely dead. Device remains useful
 *   for diagnostics even if one subsystem fails. Enables BT-only mode,
 *   audio-only mode, etc. Field robustness over brittleness.
 ********************************/

void app_main(void)
{
    /* Free BLE controller memory to give Classic BT more DRAM.
     * WHY: This application uses only Bluetooth Classic (A2DP), not BLE.
     * ESP32 memory is limited; releasing unused BLE controller memory (21KB+)
     * reduces Classic BT stack pressure and prevents out-of-memory errors
     * during streaming. Must be called before esp_bt_controller_init(). */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));    

    /* Very early unbuffered boot marker for programmatic test harness.
     * WHY: External test tools need a synchronization point to know when the
     * device has actually booted (vs. bootloader output or previous session).
     * This marker appears BEFORE subsystem init to prevent test scripts from
     * sending commands before the device is ready to process them.
     * Use both printf and ROM-level esp_rom_printf when available for maximum
     * reliability across different console configurations. */
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
     * 
     * ERROR HANDLING: UART is a platform service (fail-fast). If UART driver
     * installation fails, the device cannot function (cmd interface dead,
     * no control/diagnostics). ESP_ERROR_CHECK enforces this contract.
     */
#ifdef ESP_PLATFORM
    const int uart_rx_buf = 1024;
    const int uart_tx_buf = 1024;
    
#ifdef CONFIG_ESP_CONSOLE_UART_NUM
    const int console_uart = CONFIG_ESP_CONSOLE_UART_NUM;
#else
    const int console_uart = UART_NUM_0;
#endif

    ESP_ERROR_CHECK(uart_driver_install(console_uart, uart_rx_buf, uart_tx_buf, 0, NULL, 0));
    
    /* Success markers for test harness: UART driver installed successfully */
    printf("DIAG|BOOT|UART_INSTALL_SUCCESS|installed=1\r\n");
#ifdef CONFIG_IDF_TARGET_ESP32
    esp_rom_printf("DIAG|BOOT|UART_INSTALL_SUCCESS|installed=1\r\n");
#endif

    /* Emit critical marker for test harness: UART driver is ready, cmd layer
     * can now perform synchronous I/O without driver installation races. */
    const char ready[] = "DIAG|BOOT|UART_READY_FOR_CMD_LAYER\r\n";
    uart_write_bytes(console_uart, ready, sizeof(ready)-1);
    uart_wait_tx_done(console_uart, pdMS_TO_TICKS(50));
#endif
    
    /* ========== Platform Services Initialization ==========
     * WHY: Platform services (NVS, UART) are foundational resources that
     * multiple subsystems depend on. main.c owns these to ensure single
     * initialization and clear ownership - prevents race conditions and
     * redundant init calls that could cause errors.
     * 
     * NVS (Non-Volatile Storage) must be initialized ONCE before any
     * component accesses persistent config (bt_manager for pairing info,
     * audio_processor for pin overrides, etc.). nvs_storage_init() handles
     * version mismatch and partition erase internally.
     */
    ESP_ERROR_CHECK(nvs_storage_init());
    
    /* ========== Command Interface Initialization ==========
     * WHY INIT ORDER MATTERS: Command interface is the "control plane" and
     * must be ready BEFORE the "data plane" (BT/Audio subsystems) starts.
     * 
     * Problem if reversed: BT manager would initialize and be ready to accept
     * connections, but commands like SCAN/PAIR/VOLUME wouldn't work yet.
     * Test harness and users would see confusing failures.
     * 
     * Correct order (control → data):
     *   1. cmd_init() - command parser ready
     *   2. cmd_process_task - command processing loop starts
     *   3. bt_manager_init() - BT subsystem can now be controlled
     *   4. audio_processor_init() - audio can now be controlled
     * 
     * This ensures commands are always available when subsystems need them.
     * 
     * ERROR HANDLING: cmd_init() is a subsystem (graceful degrade). If it
     * fails, we skip task creation and continue boot - device will function
     * without command interface (BT/Audio can still work). Failure is rare
     * (cmd_init() currently has no failure paths), but we check defensively.
     */
#ifdef ESP_PLATFORM
    cmd_status_t cmd_result = cmd_init();
    if (cmd_result != CMD_SUCCESS) {
        ESP_LOGE(BT_AV_TAG, "cmd_init() failed (code=%d) - command interface unavailable", cmd_result);
        printf("ERROR|CMD_IF|INIT_FAILED|code=%d\r\n", cmd_result);
#ifdef CONFIG_IDF_TARGET_ESP32
        esp_rom_printf("ERROR|CMD_IF|INIT_FAILED|code=%d\r\n", cmd_result);
#endif
        ESP_LOGW(BT_AV_TAG, "Device will boot without command interface - BT/Audio may still function");
        // Skip cmd task creation - device continues but cmd interface dead
    } else {
        // cmd_init() succeeded - proceed with task creation
        printf("INFO|CMD_IF|BOOT_DIAG|CMD_INIT_SUCCESS\r\n");
#ifdef CONFIG_IDF_TARGET_ESP32
        esp_rom_printf("INFO|CMD_IF|BOOT_DIAG|CMD_INIT_SUCCESS\r\n");
#endif

        // Create command processing task - this starts the command interface
        // event loop so commands can be processed as soon as they arrive.
        // ERROR HANDLING: Task creation can fail (heap/stack exhausted). Check
        // return value and emit diagnostics. Device continues boot (graceful
        // degrade) but cmd processing unavailable.
        BaseType_t task_created = xTaskCreate(cmd_process_task, "cmd_proc", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
        if (task_created != pdPASS) {
            ESP_LOGE(BT_AV_TAG, "Failed to create cmd_process_task - heap/stack exhausted?");
            printf("ERROR|CMD_IF|TASK_CREATE_FAILED\r\n");
#ifdef CONFIG_IDF_TARGET_ESP32
            esp_rom_printf("ERROR|CMD_IF|TASK_CREATE_FAILED\r\n");
#endif
            ESP_LOGW(BT_AV_TAG, "Device will boot without cmd processing - BT/Audio may still function");
        } else {
            // Task created successfully - emit success markers
            printf("INFO|CMD_IF|CMD_TASK_STARTED\r\n");
#ifdef CONFIG_IDF_TARGET_ESP32
            esp_rom_printf("INFO|CMD_IF|CMD_TASK_STARTED\r\n");
#endif
        }
    }
#endif

    /* ========== Bluetooth Initialization ==========
     * WHY NOW: Command interface is ready (control plane), so BT can safely
     * initialize knowing that users/tests can immediately control it via
     * SCAN/PAIR/CONNECT commands. BT stack initialization is slow (~1-2 sec)
     * and would block if done earlier.
     * 
     * WHY BEFORE AUDIO: BT manager owns the A2DP connection state. Audio
     * streaming can't start until BT is ready. If audio init fails, BT can
     * still be useful for diagnostics and manual pairing.
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
     * WHY CONFIGURABLE AUTOSTART: Audio/I2S initialization allocates DMA
     * buffers, configures hardware pins, and starts interrupt handlers.
     * Not all deployments need audio immediately at boot:
     *   - Test harnesses may want to control init timing
     *   - Battery-powered devices may defer until actually needed
     *   - Multi-function devices may use BT for other purposes first
     * 
     * WHY LAST: Audio depends on both BT (for streaming) and CMD (for control).
     * Initializing audio before its dependencies would require complex
     * synchronization. Init order: Platform → Control → BT → Audio ensures
     * each layer has what it needs.
     * 
     * Configuration hierarchy (highest precedence first):
     * 1. NVS runtime setting - user can toggle via AUDIO_AUTOSTART command
     * 2. Kconfig compile-time default - project-wide default via menuconfig
     * 
     * load_audio_boot_config() encapsulates all policy (pins/rate/volume).
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
    ESP_LOGI(BT_AV_TAG, "ESP32 Bluetooth Audio Source - Ready");
    ESP_LOGI(BT_AV_TAG, "Use SCAN/PAIR/CONNECT commands to control BT");
    ESP_LOGI(BT_AV_TAG, "Use PLAY/VOLUME commands to control audio");
    ESP_LOGI(BT_AV_TAG, "====================================================");
    
    /* app_main() returns to FreeRTOS scheduler.
     * WHY THIS IS SAFE: FreeRTOS tasks (cmd_process_task, BT stack tasks,
     * audio I2S tasks) have already been created and are running. The
     * scheduler keeps them alive. An infinite loop here would waste CPU
     * cycles and stack space for no benefit. */
}
