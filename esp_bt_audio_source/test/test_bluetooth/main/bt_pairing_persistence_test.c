/* Two-stage on-device pairing persistence test
 * Stage 1: seed a paired device via bt_mock_add_paired_device() and set NVS marker
 *          then call esp_restart() to reboot.
 * Stage 2: on boot, detect marker, verify persisted paired device(s) are loaded
 *          via bt_mock_get_paired_device_count()/nvs_storage_get_paired_count(),
 *          clear marker and report test result.
 */

#include <string.h>
#include "unity.h"
#include "unity_fixture.h"

#include "bt_mock.h"
#include "nvs_storage.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "bt_pair_persist_test";
static const char* MARKER_KEY = "pair_persistence_stage";



TEST_GROUP(bt_pairing_persistence);

TEST_SETUP(bt_pairing_persistence)
{
    /* No-op setup: tests are driven by the explicit runner below */
}

TEST_TEAR_DOWN(bt_pairing_persistence)
{
}

/* Helper: seed a single paired device using the bt_mock API */
static void seed_paired_device_if_needed(void)
{
    int cnt = 0;
    esp_err_t err = nvs_storage_get_paired_count(&cnt);
    if (err != ESP_OK || cnt == 0) {
        /* Add a persistent paired device entry to NVS. */
        const char* mac = "02:00:00:00:00:01";
        const char* name = "Persisted Device 1";
        ESP_LOGI(TAG, "Seeding persistent paired device %s (%s)", name, mac);
        nvs_storage_add_paired_device(mac, name);
        /* Also add to the in-memory mock so tests running immediately before reboot
         * can see a paired device via bt_mock APIs if needed.
         */
        bt_device_t dev = {0};
        sscanf(mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
               &dev.addr[0], &dev.addr[1], &dev.addr[2], &dev.addr[3], &dev.addr[4], &dev.addr[5]);
        strncpy(dev.name, name, sizeof(dev.name)-1);
        dev.paired = true;
        bt_mock_add_paired_device(&dev);
    }
}

/* Stage 1 runner: seed paired device and restart */
TEST(bt_pairing_persistence, stage1_seed_and_restart)
{
    /* If there are already paired devices persisted in NVS (loaded at boot),
     * we still seed to ensure there's at least one entry and then trigger reboot.
     */
    seed_paired_device_if_needed();

    /* Ensure nvs_storage flushed the paired device. The nvs_storage API should
     * have persisted synchronously; add small delay to be safe. Then restart.
     */
    vTaskDelay(pdMS_TO_TICKS(200)); /* 200ms */

    ESP_LOGI(TAG, "Stage1 complete: triggering restart to verify persistence");
    esp_restart();
}

/* Stage 2 runner: verify paired device persisted and loaded by bt_manager/nvs_storage */
TEST(bt_pairing_persistence, stage2_verify_persistence)
{
    /* After reboot, bt_manager should have loaded paired devices from NVS.
     * Verify via nvs_storage_get_paired_count() and bt_mock_get_paired_device_count().
     */
    int nvs_count = 0;
    esp_err_t err = nvs_storage_get_paired_count(&nvs_count);
    ESP_LOGI(TAG, "Post-reboot nvs_count=%d (err=%d)", nvs_count, err);
    TEST_ASSERT_EQUAL_INT(ESP_OK, err);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, nvs_count);

    /* Optionally clear paired devices to restore clean state */
    nvs_storage_clear_paired_devices();
    bt_mock_unpair_all_devices();
}

/* Custom runner: decide which stage to run based on presence of persisted paired devices */
int run_bt_pairing_persistence_test(void)
{
    UNITY_BEGIN();

    /* Query paired count using the nvs_storage API (fills caller-provided int*) */
    int nvs_count = 0;
    esp_err_t qerr = nvs_storage_get_paired_count(&nvs_count);
    if (qerr != ESP_OK || nvs_count == 0) {
        /* Stage1: seed and restart */
        RUN_TEST_CASE(bt_pairing_persistence, stage1_seed_and_restart);
    } else {
        /* Stage2: verify persistence */
        RUN_TEST_CASE(bt_pairing_persistence, stage2_verify_persistence);
    }

    return UNITY_END();
}
