// Add proper cleanup after tests complete

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bt_mock_devices.h"

// ... existing includes and declarations ...

void app_main(void) {
    ESP_LOGI(TAG, "Starting Bluetooth Audio Source Test Suite");
    
    // Run tests
    // ... existing test runs ...
    
    // Ensure proper cleanup - add this at the end of your app_main() function
    bt_mock_cleanup();
    
    ESP_LOGI(TAG, "All tests completed. Test application will now restart.");
    // Short delay before restarting to allow logs to flush
    vTaskDelay(pdMS_TO_TICKS(100));
}
