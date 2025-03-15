#include "bluetooth/bt_app_monitor.h"
#include <stddef.h>
#include "bluetooth/bt_app_global.h"
#include "custom_log.h"

static const char *TAG = "BT_APP_MONITOR";

// Create a new timer task to periodically check for memory issues
void memory_monitor_task(void *arg) {
    while(1) {
        size_t free_heap = esp_get_free_heap_size();
        size_t min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
        SAFE_ESP_LOGI(TAG, "Free heap: %u bytes, Minimum free heap: %u bytes", 
                        free_heap, min_free_heap);
        if (free_heap < 20000) { // 20KB is a critical threshold
            s_severe_congestion = true;
            s_last_congestion_time = (uint32_t)(esp_timer_get_time() / 1000);
            SAFE_ESP_LOGW(TAG, "Memory critically low (%u bytes). Enforcing congestion control.", 
                     free_heap);
        }
        vTaskDelay(pdMS_TO_TICKS(MEMORY_CHECK_INTERVAL_MS));
    }
}

void bt_app_conn_start_memory_monitor(void) {
    xTaskCreate(memory_monitor_task, "mem_mon", 2048, NULL, 5, NULL);
}