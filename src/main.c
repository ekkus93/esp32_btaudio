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
#include "wifi.h"         // Include Wi-Fi functionality
#include "ping.h"         // Include Ping functionality
#include "esp_idf_version.h"

extern void bt_av_hdl_stack_evt(uint16_t event, void *p_param);  // << New extern declaration

#define BT_APP_STACK_UP_EVT 0x0000  // << New definition

#define UART_NUM UART_NUM_0
#define BUF_SIZE (1024)
#define LED_GPIO GPIO_NUM_2  // Replace with your LED GPIO number
#define TAG "MAIN"

void print_help() {
    printf("Available commands:\n");
    printf("  scan                 - Start Bluetooth device discovery\n");
    printf("  pair <MAC> [true|false] - Pair with a Bluetooth device (require PIN: true/false)\n");
    printf("  connect <MAC>        - Connect to a paired Bluetooth device\n");
    printf("  disconnect           - Disconnect from the connected Bluetooth device\n");
    printf("  unpair               - Unpair the connected Bluetooth device\n");
    printf("  set_wifi <SSID> <PASSWORD> - Set Wi-Fi credentials\n");
    printf("  clear_wifi           - Clear Wi-Fi credentials\n");
    printf("  connect_wifi         - Connect to Wi-Fi\n");
    printf("  disconnect_wifi      - Disconnect from Wi-Fi\n");
    printf("  show_ip              - Show the current IP address\n");
    printf("  ping <HOST> [COUNT]  - Ping a host (default count: 4)\n");
    printf("  help                 - Show this help message\n");
}

// Entry point function for PlatformIO (for ESP-IDF, use app_main())
void app_main(void) {
    ESP_LOGI(TAG, "app_main started");

    esp_err_t ret = bluetooth_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth initialization failed: %s", esp_err_to_name(ret));
        return;
    }

    // Initialize Wi-Fi
    ret = wifi_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi initialization failed: %s", esp_err_to_name(ret));
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
            } else if (strncmp((char*)data, "connect ", 8) == 0) {
                char *mac_str = strtok((char*)data + 8, " ");
                ESP_LOGI(TAG, "Connecting to device: %s", mac_str);
                esp_err_t ret = bluetooth_connect_device(mac_str);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to connect to device: %s", esp_err_to_name(ret));
                }
            } else if (strcmp((char*)data, "disconnect") == 0) {
                ESP_LOGI(TAG, "Disconnecting from device...");
                esp_err_t ret = bluetooth_disconnect_device();
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to disconnect: %s", esp_err_to_name(ret));
                }
            } else if (strcmp((char*)data, "unpair") == 0) {
                ESP_LOGI(TAG, "Unpairing device...");
                esp_err_t ret = bluetooth_unpair_device();
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to unpair: %s", esp_err_to_name(ret));
                }
            } else if (strncmp((char*)data, "set_wifi ", 9) == 0) {
                char *ssid = strtok((char*)data + 9, " ");
                char *password = strtok(NULL, " ");
                ESP_LOGI(TAG, "Setting Wi-Fi credentials: SSID=%s, PASSWORD=%s", ssid, password);
                esp_err_t ret = wifi_set_credentials(ssid, password);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to set Wi-Fi credentials: %s", esp_err_to_name(ret));
                }
            } else if (strcmp((char*)data, "clear_wifi") == 0) {
                ESP_LOGI(TAG, "Clearing Wi-Fi credentials...");
                esp_err_t ret = wifi_clear_credentials();
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to clear Wi-Fi credentials: %s", esp_err_to_name(ret));
                }
            } else if (strcmp((char*)data, "connect_wifi") == 0) {
                ESP_LOGI(TAG, "Connecting to Wi-Fi...");
                esp_err_t ret = wifi_connect();
                if (ret == ESP_OK) {
                    char ip[16];
                    if (wifi_get_ip(ip, sizeof(ip)) == ESP_OK) {
                        ESP_LOGI(TAG, "Connected with IP Address: %s", ip);
                    } else {
                        ESP_LOGE(TAG, "Failed to get IP address");
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to connect to Wi-Fi: %s", esp_err_to_name(ret));
                }
            } else if (strcmp((char*)data, "disconnect_wifi") == 0) {
                ESP_LOGI(TAG, "Disconnecting from Wi-Fi...");
                esp_err_t ret = wifi_disconnect();
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to disconnect from Wi-Fi: %s", esp_err_to_name(ret));
                }
            } else if (strcmp((char*)data, "show_ip") == 0) {
                char ip[16];
                if (wifi_get_ip(ip, sizeof(ip)) == ESP_OK) {
                    printf("Current IP Address: %s\n", ip);
                } else {
                    printf("Failed to get IP address\n");
                }
            } else if (strncmp((char*)data, "ping ", 5) == 0) {
                char *host = strtok((char*)data + 5, " ");
                char *count_str = strtok(NULL, " ");
                int count = (count_str != NULL) ? atoi(count_str) : 4;  // Default to 4 pings if count is not provided
                ESP_LOGI(TAG, "Pinging host: %s with count: %d", host, count);
                esp_err_t ret = ping_host(host, count);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to ping host: %s", esp_err_to_name(ret));
                }
            } else if (strcmp((char*)data, "help") == 0) {
                print_help();
            } else {
                ESP_LOGI(TAG, "Unknown command: %s", data);
                print_help();
            }
        }
    }

    free(data);

    // Shut down the Bluetooth application task
    bt_app_task_shut_down();
}