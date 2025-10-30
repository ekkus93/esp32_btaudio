#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "bt_app_core.h"
#include "bt_source.h"
#include "nvs_storage.h"

/* Forward declarations for external components */
extern void bt_connection_manager_init(esp_a2d_cb_t conn_cb, esp_a2d_source_data_cb_t audio_cb);
extern void bt_streaming_manager_init(void);
extern void bt_connection_state_handler(esp_a2d_connection_state_t state, esp_bd_addr_t bd_addr);
extern void bt_audio_state_handler(esp_a2d_audio_state_t state, esp_bd_addr_t bd_addr);

/* Static function declarations */



/* Public API implementations
 *
 * NOTE: The project contains an alternative implementation of the bt_* APIs
 * (components/bt_manager). To avoid duplicate symbol definitions when that
 * component is built, the public wrappers implemented here are intentionally
 * disabled. The bt_manager component provides the concrete implementations
 * expected by other components (command_interface, etc.).
 */
#if 0
esp_err_t bt_init(void)
{
    /* Implementation provided by bt_manager component */
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bt_deinit(void)
{
    esp_err_t ret;
    
    /* Shutdown A2DP */
    ret = esp_a2d_source_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "A2DP source deinit failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Shutdown AVRCP */
    ret = esp_avrc_ct_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AVRCP controller deinit failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Shutdown Bluetooth task */
    bt_app_task_shut_down();
    
    /* Disable and deinitialize Bluetooth */
    ret = esp_bluedroid_disable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid disable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_bluedroid_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid deinit failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_bt_controller_disable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller disable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_bt_controller_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller deinit failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Bluetooth deinitialized");
    return ESP_OK;
}

esp_err_t bt_connect(const char* addr)
{
    if (addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Convert address string to ESP format */
    esp_bd_addr_t bda;
    if (sscanf(addr, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
               &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) != ESP_BD_ADDR_LEN) {
        ESP_LOGE(TAG, "Invalid address format: %s", addr);
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Connect to device */
    esp_err_t ret = esp_a2d_source_connect(bda);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to device: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Connecting to device: %s", addr);
    return ESP_OK;
}

esp_err_t bt_disconnect(void)
{
    uint8_t remote_bda[ESP_BD_ADDR_LEN] = {0};
    /* Since we need to provide an address, use zeros if we don't have a specific one */
    
    /* Disconnect from device */
    esp_err_t ret = esp_a2d_source_disconnect(remote_bda);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disconnect: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Disconnecting from device");
    return ESP_OK;
}
#endif
