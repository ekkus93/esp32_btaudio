#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"      // Added for mutex
#include "driver/gpio.h"
#include "driver/uart.h"
#include "string.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "bluetooth.h"  // Include the Bluetooth header
#include "esp_idf_version.h"

#define UART_NUM UART_NUM_0
#define BUF_SIZE (1024)
#define LED_GPIO GPIO_NUM_2  // Replace with your LED GPIO number
#define TAG "MAIN"

void app_main(void)
{
    ESP_LOGI(TAG, "app_main started");

    esp_err_t ret = bluetooth_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth initialization failed");
        // Handle initialization failure
        return;
    }

    ESP_LOGI(TAG, "Bluetooth initialized successfully");

    // Initialize Bluetooth
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);

    // Initialize Bluedroid
    esp_bluedroid_init();
    esp_bluedroid_enable();

    // Register GAP event handler
    esp_bt_gap_register_callback(gap_event_handler);  // Now recognized

    // Start device discovery or other GAP operations as needed
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);

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
                ESP_LOGI(TAG, "Pairing with device: %s, require PIN: %s", mac_str, require_pin ? "true" : "false");
                esp_err_t ret = bluetooth_pair_device(mac_str, require_pin);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to pair with device: %s", esp_err_to_name(ret));
                }
            }
        }
    }

    free(data);
}