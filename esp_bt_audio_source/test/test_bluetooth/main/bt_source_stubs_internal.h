/* bt_source_stubs_internal.h — shared internals for the split bt_source_stubs
 * translation units (core .c + a2dp/gap/scan/conn domain files). NOT a public
 * header. Centralizes includes, the BT_WEAK_FN/DIAG_LOG macros, capacity
 * constants, the conditional bt_mock_* prototypes, and the mock-stub state as
 * extern (definitions kept in bt_source_stubs.c, de-static'd; the 6 names that
 * clash with bt_source_mock.c's now-global state are renamed s_stub_*). */
#ifndef BT_SOURCE_STUBS_INTERNAL_H
#define BT_SOURCE_STUBS_INTERNAL_H

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h> // For PRIu32 macros
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "bt_source.h"
#include "nvs_flash.h"
#include "nvs.h"
/* Include component-provided mock prototypes when available so delegated
 * calls to bt_mock_* are declared. This header defines BT_MOCK_PROVIDES_PROTOTYPES
 * which the file already checks in guarded delegation branches.
 */
#include "bt_mock.h"
/* The bt_mock header pulls in bt_mock_devices.h in the component, but some
 * build configurations expose the component header via the components/bluetooth
 * include path. Include the device-level prototype header too to ensure all
 * delegated bt_mock_* symbols (bt_mock_start_pairing, bt_mock_send_pin,
 * bt_mock_is_device_paired, etc.) are declared and avoid implicit
 * declaration errors during compilation.
 */
#include "bt_mock_devices.h"
#include "bt_api.h"

/* Enable DIAG_LOG-guarded diagnostic prints (matches the original single TU). */
#define DIAG_LOG

/* Weak attribute so real implementations (bt_source_mock.c) override these stubs. */
#define BT_WEAK_FN __attribute__((weak))

#define MAX_TEST_DEVICES 32
#define MAX_PAIRED_DEVICES 32

#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
/* pairing helpers */
extern esp_err_t bt_mock_start_pairing(const char* addr);
extern esp_err_t bt_mock_send_pin(const char* pin);
/* paired-device query */
extern bool bt_mock_is_device_paired(const char* addr);
/* unpair helpers */
extern esp_err_t bt_mock_unpair_device(const char* addr);
/* connection visibility helper */
extern void bt_mock_release_disconnect_visibility(void);
#endif


/* Defined in bt_source_stubs.c; called from the conn stub. */
void bt_source_stub_sync_connected_state(bool connected, const char* addr, const char* name);

/* Shared stub state — definitions live in bt_source_stubs.c (de-static'd). */
extern uint32_t s_diag_seq;
extern bt_device_t s_devices[MAX_TEST_DEVICES];
extern int s_device_count;
extern bt_device_t s_paired_devices[MAX_PAIRED_DEVICES];
extern int s_stub_paired_device_count;
extern bt_connection_state_t s_stub_connection_state;
extern bt_streaming_state_t s_stub_streaming_state;
extern bt_pairing_state_t s_pairing_state;
extern bt_pairing_method_t s_pairing_method;
extern char s_connected_device_addr[18];
extern char s_connected_device_name[32];
extern char s_default_pin[8];
extern bool s_is_scanning;
extern bool s_auto_reconnect;
extern bool s_is_connected;
extern bool s_stub_defer_disconnect_visibility;
extern bool s_ssp_supported;
extern bool s_simulate_pairing_failure;
extern bool s_simulate_pairing_timeout;
extern bool s_test_mode;
extern bool s_stub_ssp_confirmation_requested;
extern char s_stub_ssp_passkey[8];
extern bt_discovery_cb_t scan_callback;
extern void *scan_callback_data;
extern TaskHandle_t s_discovery_task_handle;
extern uint32_t s_passkey;
extern bool s_waiting_for_confirmation;
extern const char* s_connect_by_name_address;
extern const char* s_connect_by_name_name;

#endif /* BT_SOURCE_STUBS_INTERNAL_H */
