#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>  // Add this include for errno
#include <sys/stat.h>  // Add this include for struct stat
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"  // Add this include for SemaphoreHandle_t
#include "driver/gpio.h"  // Add this include for GPIO functions
#include "driver/uart.h"  // Add this include for UART functions
#include "bluetooth/bt_app_core.h"
#include "bluetooth/bt_app_av.h"
#include "bluetooth/bt_app_audio.h"
#include "bluetooth/bt_app_monitor.h"
#include "bluetooth/bt_app_conn.h"
#include "bluetooth/bt_app_global.h"  // Add this include for global variables
#include "bluetooth/bt_app_init.h"  
#include "bluetooth/bt_app_name.h"
#include "bluetooth/bt_app_discovery.h"
#include "wifi.h"
#include "ping_utils.h"
#include "spiffs_utils.h"
#include "custom_log.h"
#include "radio.h"
#include "esp_heap_caps.h"  // Add this include for heap integrity checks

#define UART_NUM UART_NUM_0
#define BUF_SIZE (1024)
#define LED_GPIO GPIO_NUM_2
#define TAG "MAIN"

// Function prototypes
void print_help_message(void);
void print_pairing_guide(void);
void handle_command(char *cmd);
bool take_bt_resource_mutex(TickType_t timeout);
void give_bt_resource_mutex(void);

// Global mutex to protect Bluetooth resources from concurrent access
SemaphoreHandle_t s_bt_resource_mutex = NULL;

/**
 * @brief Configures the logging levels for different modules
 * 
 * Sets appropriate ESP-IDF logging levels for various components to control
 * the verbosity of log output. This allows for filtering logs by importance.
 */
static void configure_log_levels(void) {
    // Configure logging levels centrally in one place
    esp_log_level_set("BT_APP_CONN", ESP_LOG_INFO);  
    esp_log_level_set("BT_APP_CORE", ESP_LOG_INFO);
    esp_log_level_set("BT_APP_AV", ESP_LOG_INFO);
    esp_log_level_set("BT_APP_AUDIO", ESP_LOG_INFO);
    esp_log_level_set("MAIN", ESP_LOG_INFO);
    esp_log_level_set("BTDM_INIT", ESP_LOG_INFO);
    esp_log_level_set("GAP", ESP_LOG_INFO);
    esp_log_level_set("A2DP", ESP_LOG_INFO);
}

/**
 * @brief Displays available commands and their descriptions
 * 
 * Prints a formatted help message to the console showing all supported
 * commands with brief descriptions of their functionality and syntax.
 * This function is called when the user enters the "help" command.
 */
void print_help_message(void) {
    printf("Available commands:\n");
    printf("  help               : Display this help message\n");
    printf("  scan               : Start Bluetooth device discovery\n");
    printf("  pair <mac_address> [pin]: Pair with a Bluetooth device (optional PIN)\n");
    printf("  connect [<mac_address>]: Connect to a paired Bluetooth device (optional MAC address)\n");
    printf("  disconnect         : Disconnect from the currently connected device\n");
    printf("  unpair             : Unpair the currently connected device\n");
    printf("  set_bt_name <name> : Set the Bluetooth device name\n");
    printf("  get_name           : Get the Bluetooth device name\n");
    printf("  volume_up          : Increase the volume\n");
    printf("  volume_down        : Decrease the volume\n");
    printf("  set_volume <level> : Set the volume level (0-127)\n");
    printf("  get_volume         : Get the current volume level\n");
    printf("  get_connected_mac  : Display the MAC address of the connected device\n");
    printf("  restart            : Restart the Bluetooth stack\n");
    printf("  set_wifi <ssid> <password> : Set Wi-Fi credentials\n");
    printf("  clear_wifi         : Clear Wi-Fi credentials\n");
    printf("  connect_wifi       : Connect to Wi-Fi\n");
    printf("  disconnect_wifi    : Disconnect from Wi-Fi\n");
    printf("  show_ip            : Show current IP address\n");
    printf("  ping <host> [count]: Ping a host (optional count)\n");
    printf("  beep [count]       : Send a beep (optional count)\n");
    printf("  play_radio <url>   : Play radio stream from URL\n");
    printf("  play_snd           : Play sound.mp3 from SPIFFS\n");
    printf("  ls_spiffs          : List files on SPIFFS partition\n");
    printf("  pairing_guide      : Show Bluetooth pairing instructions\n");
}

void print_pairing_guide(void) {
    printf("\n===============================================\n");
    printf("BLUETOOTH PAIRING INSTRUCTIONS:\n");
    printf("1. Use the 'scan' command to find available devices\n");
    printf("2. Put your audio device into pairing mode\n");
    printf("3. Note the MAC address from the scan results\n");
    printf("4. Use 'pair XX:XX:XX:XX:XX:XX' to connect\n");
    printf("5. For PIN pairing, use 'pair XX:XX:XX:XX:XX:XX true'\n");
    printf("===============================================\n");
}

#include <ctype.h> // Include for isspace

// Function to trim leading and trailing whitespace from a string
char *trim(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;

    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    end[1] = '\0';
    return str;
}

void handle_command(char *cmd) {
    // Trim trailing newline characters
    cmd[strcspn(cmd, "\r\n")] = '\0';

    SAFE_ESP_LOGI(TAG, "###Received command: %s", cmd);

    if (strcmp(cmd, "scan") == 0) {
        SAFE_ESP_LOGI(TAG, "Stopping any streaming before scan...");
        esp_err_t ret = radio_stop();  // Stop radio if playing
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "radio_stop failed (%s), aborting scan", esp_err_to_name(ret));
            return; // Prevent proceeding to scan
        }
        SAFE_ESP_LOGI(TAG, "Radio streaming stopped successfully.");
        SAFE_ESP_LOGI(TAG, "Starting Bluetooth scan...");
        ret = bluetooth_safe_start_discovery();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to start Bluetooth scan: %s", esp_err_to_name(ret));
        }
    } else if (strncmp(cmd, "pair ", 5) == 0) {
        char *mac_str = strtok(cmd + 5, " ");
        char *pin_str = strtok(NULL, " ");
        bool require_pin = (pin_str != NULL && strcmp(pin_str, "true") == 0);
        SAFE_ESP_LOGI(TAG, "Stopping Bluetooth discovery before pairing...");
        esp_bt_gap_cancel_discovery();
        SAFE_ESP_LOGI(TAG, "Pairing with device: %s, require PIN: %s", mac_str, require_pin ? "true" : "false");
        esp_err_t ret = bluetooth_pair_device(mac_str, require_pin);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to pair with device: %s", esp_err_to_name(ret));
        }
    } else if (strncmp(cmd, "connect ", 8) == 0) {
        char *mac_str = strtok(cmd + 8, " ");
        SAFE_ESP_LOGI(TAG, "Connecting to device: %s", mac_str);
        esp_err_t ret = bluetooth_connect_device(mac_str);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to connect to device: %s", esp_err_to_name(ret));
        }
    } else if (strcmp(cmd, "disconnect") == 0) {
        SAFE_ESP_LOGI(TAG, "Disconnecting from device...");
        esp_err_t ret = bluetooth_disconnect_device();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to disconnect: %s", esp_err_to_name(ret));
        }
    } else if (strcmp(cmd, "unpair") == 0) {
        SAFE_ESP_LOGI(TAG, "Unpairing device...");
        esp_err_t ret = bluetooth_unpair_device();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to unpair: %s", esp_err_to_name(ret));
        }
    } else if (strncmp(cmd, "set_wifi ", 9) == 0) {
        char *ssid = strtok(cmd + 9, " ");
        char *password = strtok(NULL, " ");
        SAFE_ESP_LOGI(TAG, "Setting Wi-Fi credentials: SSID=%s, PASSWORD=%s", ssid, password);
        esp_err_t ret = wifi_set_credentials(ssid, password);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to set Wi-Fi credentials: %s", esp_err_to_name(ret));
        }
    } else if (strcmp(cmd, "clear_wifi") == 0) {
        SAFE_ESP_LOGI(TAG, "Clearing Wi-Fi credentials...");
        esp_err_t ret = wifi_clear_credentials();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to clear Wi-Fi credentials: %s", esp_err_to_name(ret));
        }
    } else if (strcmp(cmd, "connect_wifi") == 0) {
        SAFE_ESP_LOGI(TAG, "Connecting to Wi-Fi...");
        esp_err_t ret = wifi_connect();
        if (ret == ESP_OK) {
            char ip[16];
            if (wifi_get_ip(ip, sizeof(ip)) == ESP_OK) {
                SAFE_ESP_LOGI(TAG, "Connected with IP Address: %s", ip);
            } else {
                SAFE_ESP_LOGE(TAG, "Failed to get IP address");
            }
        } else {
            SAFE_ESP_LOGE(TAG, "Failed to connect to Wi-Fi: %s", esp_err_to_name(ret));
        }
    } else if (strcmp(cmd, "disconnect_wifi") == 0) {
        SAFE_ESP_LOGI(TAG, "Disconnecting from Wi-Fi...");
        esp_err_t ret = wifi_disconnect();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to disconnect from Wi-Fi: %s", esp_err_to_name(ret));
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
        int count = (count_str != NULL) ? atoi(count_str) : 4;
        SAFE_ESP_LOGI(TAG, "Pinging host: %s with count: %d", host, count);
        esp_err_t ret = ping_host(host, count);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to ping host: %s", esp_err_to_name(ret));
        }
    } else if (strncmp(cmd, "set_bt_name ", 12) == 0) {
        char *name = strtok(cmd + 12, " ");
        if (name) {
            SAFE_ESP_LOGI(TAG, "Setting Bluetooth device name to: %s", name);
            esp_err_t ret = bluetooth_set_device_name(name);
            if (ret != ESP_OK) {
                SAFE_ESP_LOGE(TAG, "Failed to set Bluetooth device name: %s", esp_err_to_name(ret));
            }
        } else {
            SAFE_ESP_LOGE(TAG, "Invalid Bluetooth device name");
        }
    } else if (strcmp(cmd, "restart") == 0) {
        SAFE_ESP_LOGI(TAG, "Restarting the program...");
        esp_restart();
    } else if (strcmp(cmd, "help") == 0) {
        print_help_message();
    } else if (strncmp(cmd, "beep", 4) == 0) {
        char *param = strtok(cmd + 4, " ");
        int count = 1;
        if (param) {
            count = atoi(param);
            if (count <= 0) {
                count = 1;
            }
        }
        SAFE_ESP_LOGI(TAG, "Sending %d beep(s)...", count);
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
            SAFE_ESP_LOGI(TAG, "Starting radio stream from URL: %s", url);
            char *url_copy = strdup(url);
            if (!url_copy) {
                SAFE_ESP_LOGE(TAG, "Failed to allocate memory for URL");
                return;
            }
            
            if (xTaskCreate(play_mp3_task, "radio_task", 16384, (void*)url_copy, 5, NULL) != pdPASS) {
                SAFE_ESP_LOGE(TAG, "Failed to create radio task");
                free(url_copy);
            }
        } else {
            SAFE_ESP_LOGE(TAG, "No URL provided for play_radio command");
        }
    } else if (strcmp(cmd, "play_snd") == 0) {
        mp3_play_file("sound.mp3");
    } else if (strcmp(cmd, "ls_spiffs") == 0) {
        SAFE_ESP_LOGI(TAG, "Listing files on SPIFFS partition...");
        list_spiffs_files();
    } else if (strcmp(cmd, "volume_up") == 0) {
        SAFE_ESP_LOGI(TAG, "Increasing volume");
        esp_err_t ret = bluetooth_volume_up();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to send volume up command: %s", esp_err_to_name(ret));
        }
    } else if (strcmp(cmd, "volume_down") == 0) {
        SAFE_ESP_LOGI(TAG, "Decreasing volume");
        esp_err_t ret = bluetooth_volume_down();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to send volume down command: %s", esp_err_to_name(ret));
        }
    } else if (strncmp(cmd, "set_volume ", 11) == 0) {
        char *volume_str = strtok(cmd + 11, " ");
        if (volume_str != NULL) {
            int volume = atoi(volume_str);
            if (volume >= 0 && volume <= 127) {
                SAFE_ESP_LOGI(TAG, "Setting volume to %d", volume);
                esp_err_t ret = bluetooth_set_volume((uint8_t)volume);
                if (ret != ESP_OK) {
                    SAFE_ESP_LOGE(TAG, "Failed to set volume to %d: %s", volume, esp_err_to_name(ret));
                }
            } else {
                SAFE_ESP_LOGE(TAG, "Invalid volume: %d (must be 0-127)", volume);
            }
        } else {
            SAFE_ESP_LOGE(TAG, "Usage: set_volume <VOLUME>");
        }
    } else if (strcmp(cmd, "get_volume") == 0) {
        SAFE_ESP_LOGI(TAG, "Getting current volume");
        esp_err_t ret = bluetooth_get_current_volume();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Failed to get volume: %s", esp_err_to_name(ret));
        } else {
            SAFE_ESP_LOGI(TAG, "get_volume command sent. Check logs for volume level.");
        }
    } else if (strcmp(cmd, "pairing_guide") == 0) {
        print_pairing_guide();
    } else {
        SAFE_ESP_LOGI(TAG, "Unknown command: %s", cmd);
        print_help_message();
    }
}

void app_main(void) {
    // Force enable ALL INFO logs immediately
    esp_log_level_set("*", ESP_LOG_INFO);
    
    // Configure logging system-wide
    configure_log_levels();
    
    SAFE_ESP_LOGI(TAG, "app_main started");

    uint8_t data[BUF_SIZE];

    // Create the Bluetooth resource mutex
    s_bt_resource_mutex = xSemaphoreCreateMutex();
    if (s_bt_resource_mutex == NULL) {
        SAFE_ESP_LOGE(TAG, "Failed to create Bluetooth resource mutex");
        return;
    }

    // Initialize SPIFFS
    if (init_spiffs() != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "SPIFFS initialization failed!");
        // Continue anyway - non-fatal error
    }

    // Initialize audio buffer system
    bt_app_audio_init();
    
    // Initialize Bluetooth
    esp_err_t ret = bluetooth_init();
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Bluetooth initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    // Re-enable memory monitoring
    bt_app_conn_start_memory_monitor();

    // Start up the Bluetooth application task
    bt_app_task_start_up();

    // Dispatch stack event to set up Bluetooth device name, connection mode, and profile
    bt_app_work_dispatch(bt_av_hdl_stack_evt, BT_APP_STACK_UP_EVT, NULL, 0, NULL);

    // Initialize Wi-Fi
    ret = wifi_init();
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Wi-Fi initialization failed: %s", esp_err_to_name(ret));
        return;
    }

    // Print the Bluetooth device name
    char bt_name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
    ret = bluetooth_get_device_name(bt_name, sizeof(bt_name));
    if (ret == ESP_OK) {
        SAFE_ESP_LOGI(TAG, "Bluetooth device name: %s", bt_name);
    } else {
        SAFE_ESP_LOGE(TAG, "Failed to get Bluetooth device name: %s", esp_err_to_name(ret));
    }

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

    // Configure LED GPIO
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    printf("ESP-IDF version: %d\n", ESP_IDF_VERSION);

    // Main loop
    while (1) {
        //SAFE_ESP_LOGI(TAG, "###Main loop iteration");
        // Toggle LED
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(500));

        // Poll UART for commands
        int len = uart_read_bytes(UART_NUM, data, BUF_SIZE - 1, 100 / portTICK_PERIOD_MS);
        //SAFE_ESP_LOGI(TAG, "###Received %d bytes", len);
        if (len > 0) {
            //SAFE_ESP_LOGI(TAG, "###Received data: %s", data);
            data[len] = '\0';  // Null-terminate received command
            handle_command((char*)data);
        }

        // Check heap integrity periodically
        if (!heap_caps_check_integrity_all(true)) {
            SAFE_ESP_LOGE(TAG, "Heap integrity check failed");
        }

        // Log free heap size periodically
        SAFE_ESP_LOGI(TAG, "Free heap size: %lu bytes, Minimum free heap: %u bytes", 
            (unsigned long)esp_get_free_heap_size(), heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT));
    }

    // Clean up (unreachable in current implementation)
    bt_app_task_shut_down();
}

// Mutex helper functions
// Add static variable to track mutex owner
TaskHandle_t s_bt_mutex_owner = NULL;

bool take_bt_resource_mutex(TickType_t timeout) {
    if (s_bt_resource_mutex == NULL) {
        SAFE_ESP_LOGE(TAG, "BT resource mutex not initialized");
        return false;
    }
    if (xSemaphoreTake(s_bt_resource_mutex, timeout) == pdTRUE) {
        s_bt_mutex_owner = xTaskGetCurrentTaskHandle();
        SAFE_ESP_LOGI(TAG, "Mutex acquired by task: %p, name: %s", 
                        s_bt_mutex_owner, pcTaskGetName(s_bt_mutex_owner));
        return true;
    } else {
        if (s_bt_mutex_owner != NULL) {
            SAFE_ESP_LOGE(TAG, "Failed to acquire mutex. Currently held by task: %p, name: %s", 
                        s_bt_mutex_owner, pcTaskGetName(s_bt_mutex_owner));
        } else {
            SAFE_ESP_LOGE(TAG, "Failed to acquire mutex; owner unknown");
        }
        return false;
    }
}

void give_bt_resource_mutex(void) {
    if (s_bt_resource_mutex != NULL) {
        SAFE_ESP_LOGI(TAG, "Mutex released by task: %p", xTaskGetCurrentTaskHandle());
        s_bt_mutex_owner = NULL;
        xSemaphoreGive(s_bt_resource_mutex);
    }
}

