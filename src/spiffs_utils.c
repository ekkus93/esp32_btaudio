#include "spiffs_utils.h"
#include <esp_spiffs.h>
#include <sys/dirent.h>
#include <sys/stat.h>     // Add this include for struct stat
#include "esp_log.h"
#include <errno.h>
#include <string.h> // For strlcpy

#define TAG "BT_APP"
#define MAX_SPIFFS_PATH 32
#define SPIFFS_BASE_PATH "/spiffs"

static bool g_spiffs_mounted = false;

esp_err_t init_spiffs(void) {
    if (g_spiffs_mounted) {
        return ESP_OK;  // Already mounted
    }

    ESP_LOGI(TAG, "Initializing SPIFFS");
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_BASE_PATH,
        .partition_label = "storage", // Correct partition label
        .max_files = 5,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info("storage", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS info (%s)", esp_err_to_name(ret));
        esp_vfs_spiffs_unregister("storage");
        return ret;
    }

    ESP_LOGI(TAG, "SPIFFS mounted successfully. Total: %d, used: %d", total, used);
    g_spiffs_mounted = true;
    return ESP_OK;
}

void unmount_spiffs(void) {
    if (g_spiffs_mounted) {
        esp_vfs_spiffs_unregister("storage");
        g_spiffs_mounted = false;
        ESP_LOGI(TAG, "SPIFFS unmounted");
    }
}

void list_spiffs_files(void) {
    if (!g_spiffs_mounted) {
        ESP_LOGE(TAG, "SPIFFS not mounted");
        return;
    }

    DIR *dir = opendir(SPIFFS_BASE_PATH);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open %s directory (errno=%d)", SPIFFS_BASE_PATH, errno);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        struct stat st;
        char fullpath[64]; // "/spiffs/" is 8 bytes, 32 for filename, null terminator

        // Construct the full path using snprintf (again, but with careful size management)
        int path_len = snprintf(fullpath, sizeof(fullpath), "%s/%s", SPIFFS_BASE_PATH, entry->d_name);

        // Check for snprintf errors
        if (path_len < 0 || path_len >= sizeof(fullpath)) {
            ESP_LOGE(TAG, "Path too long or snprintf error for file: %s", entry->d_name);
            continue;
        }
        
        // Log the full path to verify it's correct
        ESP_LOGI(TAG, "Checking file: %s", fullpath);

        // Attempt to get file stats
        if (stat(fullpath, &st) == 0) {
            ESP_LOGI(TAG, "Found file: %s (%ld bytes)", entry->d_name, st.st_size);
        } else {
            ESP_LOGE(TAG, "Failed to get stats for file: %s (errno=%d)", fullpath, errno);
            ESP_LOGI(TAG, "Found file: %s (size unknown)", entry->d_name);
        }
    }
    closedir(dir);
}