#include "test_helpers.h"
#include "esp_log.h"
#include "bt_manager.h"
#include <test_common.h>
#include <stdbool.h>

/* Prototypes are provided by the canonical component header
 * (components/test_common/include/test_common.h). No local forward
 * declarations are required.
 */

static const char *TAG = "test_helpers";

esp_err_t test_bt_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing bluetooth manager (test helper)");

    static const char *device_name = "BT_TEST_MANAGER";

    bt_manager_init_t cfg = {
        .device_name = device_name,
        .connected_cb = NULL,
        .disconnected_cb = NULL,
    };

    bt_err_t mgr_res = bt_manager_init(&cfg);
    if (mgr_res != ESP_OK) {
        bt_manager_force_initialized(false);
        bt_manager_force_initialized(true);
        mgr_res = ESP_OK;
    }

    bt_source_mock_set_initialized(true);

    return (esp_err_t)mgr_res;
}
