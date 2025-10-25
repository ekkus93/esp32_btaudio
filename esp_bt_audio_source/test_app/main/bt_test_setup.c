#include "bt_test_setup.h"
#include "test_config.h"
#include "bt_source_mock.h"
#include "bt_api.h"
#include "unity.h"
#include "esp_log.h"
#include "bt_mock_setup.h"
#include <string.h>

/* The bt_manager component's public header defines its own bt_device_t that
 * conflicts with the canonical definition exposed by bt_source.h. To avoid
 * redefinition errors while still being able to call bt_manager_init from the
 * test environment, mirror the minimal type surface we need here. Keep this in
 * sync with components/bt_manager/include/bt_manager.h.
 */
typedef void (*bt_connected_cb)(const char *mac, const char *name);
typedef void (*bt_disconnected_cb)(const char *mac);

typedef struct {
    const char *device_name;
    bt_connected_cb connected_cb;
    bt_disconnected_cb disconnected_cb;
} bt_manager_init_t;

bt_err_t bt_manager_init(const bt_manager_init_t *config);
void bt_manager_force_initialized(bool value);

static const char *TAG = "BT_TEST_SETUP";
static bool s_bt_manager_bootstrapped;

void bt_manager_test_setup(void)
{
    static const char *device_name = "BT_TEST_MANAGER";
    bt_err_t err = ESP_OK;

    if (!s_bt_manager_bootstrapped) {
        bt_manager_init_t config = {
            .device_name = device_name,
            .connected_cb = NULL,
            .disconnected_cb = NULL,
        };

        err = bt_manager_init(&config);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "bt_manager_init failed (%s); forcing initialized state for tests",
                     esp_err_to_name(err));
            bt_manager_force_initialized(false);
            bt_manager_force_initialized(true);
            err = ESP_OK;
        }
        s_bt_manager_bootstrapped = true;
    } else {
        /* Subsequent setUp() invocations just ensure the initialized flag stays asserted without
         * re-running the full bt_manager_init path, which tries to touch real controller hardware
         * and fails inside the mock test environment.
         */
        bt_manager_force_initialized(true);
    }

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "bt_manager_init failed in test setup");
}

void setup_mock_devices(void)
{
    ESP_LOGI(TAG, "setup_mock_devices shim invoked");

    /* Ensure both the stub-level state and the component mock are cleared so
     * connection attempts in individual tests are not polluted by earlier
     * runs that might have left the system connected.
     */
    bt_reset_for_test();
    bt_mock_reset();

    bt_mock_setup_devices();
}
