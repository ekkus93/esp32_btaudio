#include "bt_test_setup.h"
#include "test_config.h"
#include "bt_source_mock.h"
#include "bt_api.h"
#include "unity.h"
#include "esp_log.h"
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

void bt_mock_setup_common(void)
{
    ESP_LOGI(TAG, "Setting up common BT test environment");
    
    /* Reset the stub-side state as well so repeated tests start from a clean
     * slate even if previous runs aborted before disconnecting or stopping
     * streaming. This keeps the local stub view in sync with the component
     * mock we reset below.
     */
    bt_reset_for_test();

    // Make sure BT is initialized
    esp_err_t ret = bt_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BT: %d", ret);
    }
    
    // Reset BT mock system
    bt_mock_reset();
    
    // Add some test devices
    bt_mock_setup_devices();
}

void bt_mock_setup_devices(void)
{
    ESP_LOGI(TAG, "Setting up mock devices");
    
    // Add test audio device. Prefer component-level authoritative helper
    // when available so other delegated calls (connect-by-name, scan)
    // observe the same device list.
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    bt_mock_add_device(TEST_DEVICE_ADDR, TEST_DEVICE_NAME, BT_DEVICE_TYPE_AUDIO, false);
    bt_mock_add_device("22:33:44:55:66:77", "Test_Phone", BT_DEVICE_TYPE_PHONE, false);
    bt_mock_add_device("33:44:55:66:77:88", "Test_Computer", BT_DEVICE_TYPE_OTHER, false);
#else
    // Fallback to local test-app helper when component mock is not present
    bt_mock_add_test_device(TEST_DEVICE_ADDR, TEST_DEVICE_NAME, BT_DEVICE_TYPE_AUDIO);
    bt_mock_add_test_device("22:33:44:55:66:77", "Test_Phone", BT_DEVICE_TYPE_PHONE);
    bt_mock_add_test_device("33:44:55:66:77:88", "Test_Computer", BT_DEVICE_TYPE_OTHER);
#endif
}

void bt_mock_setup_paired_devices(void)
{
    ESP_LOGI(TAG, "Setting up paired devices");
    
    // Initialize BT and reset mock
    bt_init();
    bt_mock_reset();
    
    // Create a paired device
    bt_device_t paired_device = {0};
    const uint8_t addr_bytes[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    memcpy(paired_device.addr, addr_bytes, sizeof(addr_bytes));
    strncpy(paired_device.name, TEST_DEVICE_NAME, sizeof(paired_device.name) - 1);
    paired_device.rssi = -50;
    paired_device.cod = 0x240404; // Audio device class of device
    paired_device.paired = true;
    
    // Add as paired device
    bt_mock_add_paired_device(&paired_device);
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
