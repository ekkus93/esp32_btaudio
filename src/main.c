#include <stdio.h>
#include <dirent.h>  // Add this include for directory operations
#include <fcntl.h>  // Add this for O_RDONLY
#include <unistd.h> // Add this for read()
#include <errno.h>  // Add this include
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
#include "radio.h"        // Include radio functionality
#include "esp_idf_version.h"
#include "esp_spiffs.h"  
#include "spiffs_utils.h"  // Add this include

extern void bt_av_hdl_stack_evt(uint16_t event, void *p_param);  // << New extern declaration

#define BT_APP_STACK_UP_EVT 0x0000  // << New definition

#define UART_NUM UART_NUM_0
#define BUF_SIZE (1024)
#define LED_GPIO GPIO_NUM_2  // Replace with your LED GPIO number
#define TAG "MAIN"

// Remove the unused g_spiffs_mounted variable since it's now handled in spiffs_utils.c

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
    printf("  set_bt_name <NAME>   - Set the Bluetooth device name\n");
    printf("  restart              - Restart the program\n");
    printf("  help                 - Show this help message\n");
    printf("  beep [count]         - Send one or multiple beeps (default is 1 beep, only when connected)\n");
    printf("  play_radio <URL>     - Play an Internet radio stream from the provided URL\n");
    printf("  play_snd             - Play sound.mp3 from SPIFFS storage through Bluetooth\n");
    printf("  ls_spiffs            - List files on SPIFFS partition\n");
    printf("  volume_up            - Increase the Bluetooth audio volume\n");
    printf("  volume_down          - Decrease the Bluetooth audio volume\n");
    printf("  set_volume <VOLUME>  - Set the Bluetooth audio volume (0-127)\n");
    printf("  get_volume           - Get the current Bluetooth audio volume\n");
}

#define SPIFFS_BASE_PATH "/spiffs"

void handle_command(char *cmd) {
    // Trim trailing newline characters
    cmd[strcspn(cmd, "\r\n")] = '\0';

    if (strcmp(cmd, "scan") == 0) {
        ESP_LOGI(TAG, "Stopping Bluetooth audio streaming before scan...");
        esp_bt_gap_cancel_discovery();
        esp_err_t ret = bluetooth_disconnect_device();  // Ensure any ongoing Bluetooth audio streaming is stopped
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Bluetooth audio streaming stopped successfully.");
        } else {
            ESP_LOGW(TAG, "No Bluetooth audio streaming to stop.");
        }
        ret = radio_stop();  // Ensure any ongoing radio streaming is stopped
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Radio streaming stopped successfully.");
        } else {
            ESP_LOGW(TAG, "No radio streaming to stop.");
        }

        ESP_LOGI(TAG, "Starting Bluetooth scan...");
        ret = bluetooth_start_discovery();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start Bluetooth scan: %s", esp_err_to_name(ret));
        }
    } else if (strncmp(cmd, "pair ", 5) == 0) {
        char *mac_str = strtok(cmd + 5, " ");
        char *pin_str = strtok(NULL, " ");
        bool require_pin = (pin_str != NULL && strcmp(pin_str, "true") == 0);
        ESP_LOGI(TAG, "Stopping Bluetooth discovery before pairing...");
        esp_bt_gap_cancel_discovery();
        ESP_LOGI(TAG, "Pairing with device: %s, require PIN: %s", mac_str, require_pin ? "true" : "false");
        esp_err_t ret = bluetooth_pair_device(mac_str, require_pin);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to pair with device: %s", esp_err_to_name(ret));
        }
    } else if (strncmp(cmd, "connect ", 8) == 0) {
        char *mac_str = strtok(cmd + 8, " ");
        ESP_LOGI(TAG, "Connecting to device: %s", mac_str);
        esp_err_t ret = bluetooth_connect_device(mac_str);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to connect to device: %s", esp_err_to_name(ret));
        }
    } else if (strcmp(cmd, "disconnect") == 0) {
        ESP_LOGI(TAG, "Disconnecting from device...");
        esp_err_t ret = bluetooth_disconnect_device();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disconnect: %s", esp_err_to_name(ret));
        }
    } else if (strcmp(cmd, "unpair") == 0) {
        ESP_LOGI(TAG, "Unpairing device...");
        esp_err_t ret = bluetooth_unpair_device();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to unpair: %s", esp_err_to_name(ret));
        }
    } else if (strncmp(cmd, "set_wifi ", 9) == 0) {
        char *ssid = strtok(cmd + 9, " ");
        char *password = strtok(NULL, " ");
        ESP_LOGI(TAG, "Setting Wi-Fi credentials: SSID=%s, PASSWORD=%s", ssid, password);
        esp_err_t ret = wifi_set_credentials(ssid, password);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set Wi-Fi credentials: %s", esp_err_to_name(ret));
        }
    } else if (strcmp(cmd, "clear_wifi") == 0) {
        ESP_LOGI(TAG, "Clearing Wi-Fi credentials...");
        esp_err_t ret = wifi_clear_credentials();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to clear Wi-Fi credentials: %s", esp_err_to_name(ret));
        }
    } else if (strcmp(cmd, "connect_wifi") == 0) {
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
    } else if (strcmp(cmd, "disconnect_wifi") == 0) {
        ESP_LOGI(TAG, "Disconnecting from Wi-Fi...");
        esp_err_t ret = wifi_disconnect();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disconnect from Wi-Fi: %s", esp_err_to_name(ret));
        }
    } else if (strcmp(cmd, "show_ip") == 0) {
        char ip[16];
        if (wifi_get_ip(ip, sizeof(ip)) == ESP_OK) {
            printf("Current IP Address: %s\n", ip);
        } else {
            printf("Failed to get IP address\n");
        }
    } else if (strncmp(cmd, "ping ", 5) == 0) {
        char *host = strtok(cmd + 5, " ");
        char *count_str = strtok(NULL, " ");
        int count = (count_str != NULL) ? atoi(count_str) : 4;  // Default to 4 pings if count is not provided
        ESP_LOGI(TAG, "Pinging host: %s with count: %d", host, count);
        esp_err_t ret = ping_host(host, count);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to ping host: %s", esp_err_to_name(ret));
        }
    } else if (strncmp(cmd, "set_bt_name ", 12) == 0) {
        char *name = strtok(cmd + 12, " ");
        if (name) {
            ESP_LOGI(TAG, "Setting Bluetooth device name to: %s", name);
            esp_err_t ret = bluetooth_set_device_name(name);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set Bluetooth device name: %s", esp_err_to_name(ret));
            }
        } else {
            ESP_LOGE(TAG, "Invalid Bluetooth device name");
        }
    } else if (strcmp(cmd, "restart") == 0) {
        ESP_LOGI(TAG, "Restarting the program...");
        esp_restart();
    } else if (strcmp(cmd, "help") == 0) {
        print_help();
    } else if (strncmp(cmd, "beep", 4) == 0) {
        // Parse optional parameter for number of beeps
        char *param = strtok(cmd + 4, " ");
        int count = 1; // Default to 1 beep
        if (param) {
            count = atoi(param);
            if (count <= 0) {
                count = 1;
            }
        }
        ESP_LOGI(TAG, "Sending %d beep(s)...", count);
        for (int i = 0; i < count; i++) {
            if (bluetooth_send_beep() == ESP_OK) {
                printf("Beep command sent.\n");
            } else {
                printf("Failed to send beep. Ensure Bluetooth is connected.\n");
            }
            vTaskDelay(pdMS_TO_TICKS(1200)); // Delay to allow beep processing
        }
    } else if (strncmp(cmd, "play_radio ", 11) == 0) {
        char *url = strtok(cmd + 11, " ");
        if (url) {
            ESP_LOGI(TAG, "Starting radio stream from URL: %s", url);
            // Make a copy of the URL string that will persist after this function returns
            char *url_copy = strdup(url);
            if (!url_copy) {
                ESP_LOGE(TAG, "Failed to allocate memory for URL");
                return;
            }
            
            // Create task with larger stack size (16KB instead of 4KB)
            if (xTaskCreate(radio_task, "radio_task", 16384, (void*)url_copy, 5, NULL) != pdPASS) {
                ESP_LOGE(TAG, "Failed to create radio task");
                free(url_copy);
            }
        } else {
            ESP_LOGE(TAG, "No URL provided for play_radio command");
        }
    } else if (strcmp(cmd, "play_snd") == 0) {
        ESP_LOGI(TAG, "Playing sound.mp3 from SPIFFS");

        // Initialize SPIFFS with recovery options
        esp_vfs_spiffs_conf_t conf = {
            .base_path = "/spiffs",
            .partition_label = "storage",
            .max_files = 5,
            .format_if_mount_failed = true  // Format if corrupted
        };
        
        ESP_LOGI(TAG, "Mounting SPIFFS...");
        esp_err_t ret = esp_vfs_spiffs_register(&conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
            return;
        }

        // Print partition info
        size_t total = 0, used = 0;
        ret = esp_spiffs_info("storage", &total, &used);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get SPIFFS partition info (%s)", esp_err_to_name(ret));
            esp_vfs_spiffs_unregister("storage");
            return;
        }
        ESP_LOGI(TAG, "Partition total: %d, used: %d", total, used);

        if (total == 0) {
            ESP_LOGE(TAG, "SPIFFS partition is empty");
            esp_vfs_spiffs_unregister("storage");
            return;
        }

        if (total > 0 && used == 0) {
            ESP_LOGE(TAG, "SPIFFS partition is empty");
            esp_vfs_spiffs_unregister("storage");
            return;
        }

        if (used > total) {
            ESP_LOGE(TAG, "SPIFFS used space exceeds total space"); 
            esp_vfs_spiffs_unregister("storage");
            return;
        }

        /*
        // Verify SPIFFS integrity
        ret = esp_spiffs_check("storage");
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS is corrupted! (%s)", esp_err_to_name(ret));
            esp_vfs_spiffs_unregister("storage");
            return;
        }

        ESP_LOGI(TAG, "SPIFFS is intact");
        */

        // Print partition info and attempt to read
        const char* filepath = "/spiffs/sound.mp3";
        struct stat st;
        if (stat(filepath, &st) == 0) {
            ESP_LOGI(TAG, "File exists, size: %ld bytes", st.st_size);

            // Try reading with fread first
            FILE *fp = fopen(filepath, "rb");
            if (fp) {
                uint8_t buf[16];
                size_t bytes = fread(buf, 1, sizeof(buf), fp);
                if (bytes > 0) {
                    ESP_LOGI(TAG, "fread success: %d bytes", (int)bytes);
                    for (int i = 0; i < bytes; i++) {
                        printf("%02x ", buf[i]);
                    }
                    printf("\n");
                    // If fread works, continue with normal playback
                    rewind(fp);
                    radio_task_params_t *params = malloc(sizeof(radio_task_params_t));
                    if (params) {
                        params->fp = fp;
                        params->size = st.st_size;
                        if (xTaskCreate(radio_task, "radio_task", 8192, params, 5, NULL) != pdPASS) {
                            ESP_LOGE(TAG, "Failed to create radio task");
                            fclose(fp);
                            free(params);
                        }
                        return;  // Success path
                    }
                } else {
                    ESP_LOGE(TAG, "fread failed: %d", errno);
                }
                fclose(fp);
            } else {
                ESP_LOGE(TAG, "fopen failed: %d", errno);
            }
        } else {
            ESP_LOGI(TAG, "File not found or read error, attempting to read with open/read");
            // If we get here, something failed
            esp_vfs_spiffs_unregister("storage");
        }
    } else if (strcmp(cmd, "ls_spiffs") == 0) {
        ESP_LOGI(TAG, "Listing files on SPIFFS partition...");
        list_spiffs_files();
    } else if (strcmp(cmd, "volume_up") == 0) {
        ESP_LOGI(TAG, "Increasing volume");
        esp_err_t ret = bluetooth_volume_up();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send volume up command: %s", esp_err_to_name(ret));
        }
    } else if (strcmp(cmd, "volume_down") == 0) {
        ESP_LOGI(TAG, "Decreasing volume");
        esp_err_t ret = bluetooth_volume_down();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send volume down command: %s", esp_err_to_name(ret));
        }
    } else if (strncmp(cmd, "set_volume ", 11) == 0) {
        char *volume_str = strtok(cmd + 11, " ");
        if (volume_str != NULL) {
            int volume = atoi(volume_str);
            if (volume >= 0 && volume <= 127) {
                ESP_LOGI(TAG, "Setting volume to %d", volume);
                esp_err_t ret = bluetooth_set_volume((uint8_t)volume);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to set volume to %d: %s", volume, esp_err_to_name(ret));
                }
            } else {
                ESP_LOGE(TAG, "Invalid volume: %d (must be 0-127)", volume);
            }
        } else {
            ESP_LOGE(TAG, "Usage: set_volume <VOLUME>");
        }
    } else if (strcmp(cmd, "get_volume") == 0) {
        ESP_LOGI(TAG, "Getting current volume");
        esp_err_t ret = bluetooth_get_volume();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get volume: %s", esp_err_to_name(ret));
        } else {
            // Volume will be printed in the AVRCP callback
            ESP_LOGI(TAG, "get_volume command sent. Check logs for volume level.");
        }
    } else {
        ESP_LOGI(TAG, "Unknown command: %s", cmd);
        print_help();
    }
}

// Entry point function for PlatformIO (for ESP-IDF, use app_main())
void app_main(void) {
    ESP_LOGI(TAG, "app_main started");

    // Initialize SPIFFS early to ensure it's available when needed
    if (init_spiffs() != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS initialization failed!");
        // Continue anyway - non-fatal error
    }

    esp_err_t ret = bluetooth_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth initialization failed: %s", esp_err_to_name(ret));
        return;
    }

    // Print the Bluetooth device name
    char bt_name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
    ret = bluetooth_get_device_name(bt_name, sizeof(bt_name));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Bluetooth device name: %s", bt_name);
    } else {
        ESP_LOGE(TAG, "Failed to get Bluetooth device name: %s", esp_err_to_name(ret));
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
            data[len] = '\0';  // Null-terminate received command
            handle_command((char*)data);
        }
    }

    free(data);

    // Shut down the Bluetooth application task
    bt_app_task_shut_down();
}