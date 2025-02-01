#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "string.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "bt_app_core.h"  // Include the Bluetooth header
#include "bluetooth.h"    // Include additional Bluetooth functionality
#include "esp_idf_version.h"

extern void bt_av_hdl_stack_evt(uint16_t event, void *p_param);  // << New extern declaration

#define BT_APP_STACK_UP_EVT 0x0000  // << New definition

#define UART_NUM UART_NUM_0
#define BUF_SIZE (1024)
#define LED_GPIO GPIO_NUM_2  // Replace with your LED GPIO number
#define TAG "MAIN"

// Entry point function for PlatformIO (for ESP-IDF, use app_main())
void app_main(void) {
    ESP_LOGI(TAG, "app_main started");

    esp_err_t ret = bluetooth_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth initialization failed: %s", esp_err_to_name(ret));
        return;
    }

    // Start up the Bluetooth application task
    bt_app_task_start_up();

    // Dispatch stack event to set up Bluetooth device name, connection mode, and profile
    bt_app_work_dispatch(bt_av_hdl_stack_evt, BT_APP_STACK_UP_EVT, NULL, 0, NULL);

    // Initialize A2DP
    if (init_a2dp() != ESP_OK) {
        ESP_LOGE(TAG, "A2DP initialization failed");
        return;
    }
    ESP_LOGI(TAG, "A2DP initialization succeeded");

    // Configure UART
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM, &uart_config);
    uart_driver_install(UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    // Removed the uart_set_baudrate() call to keep it at 115200

    // Configure LED GPIO
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    uint8_t* data = (uint8_t*) malloc(BUF_SIZE);
    if (data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for UART data");
        return;
    }

    printf("ESP-IDF version: %d\n", ESP_IDF_VERSION);  // Changed from %s to %d

    while (1) {
        // Toggle LED
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(500));

        // Poll UART for commands
        int len = uart_read_bytes(UART_NUM, data, BUF_SIZE - 1, 100 / portTICK_PERIOD_MS);
        if (len > 0) {
            data[len] = '\0';
            
            // Remove newline characters
            char *newline = strchr((char*)data, '\n');
            if (newline) *newline = '\0';
            newline = strchr((char*)data, '\r');
            if (newline) *newline = '\0';

            if (strcmp((char*)data, "scan") == 0) {
                ESP_LOGI(TAG, "Starting Bluetooth scan...");
                esp_err_t ret = bluetooth_start_discovery();
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to start Bluetooth scan: %s", esp_err_to_name(ret));
                }
            } else if (strncmp((char*)data, "pair ", 5) == 0) {
                char *mac_str = strtok((char*)data + 5, " ");
                char *pin_str = strtok(NULL, " ");
                bool require_pin = (pin_str != NULL && strcmp(pin_str, "true") == 0);
                ESP_LOGI(TAG, "Stopping Bluetooth discovery before pairing...");
                esp_bt_gap_cancel_discovery();
                ESP_LOGI(TAG, "Pairing with device: %s, require PIN: %s", mac_str, require_pin ? "true" : "false");
                esp_err_t ret = bluetooth_pair_device(mac_str, require_pin);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to pair with device: %s", esp_err_to_name(ret));
                }
            }
        }
    }

    free(data);

    // Shut down the Bluetooth application task
    bt_app_task_shut_down();
}