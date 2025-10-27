/**
 * @file bt_source_mock.h
 * @brief Mock implementation of Bluetooth source functions for testing
 */

#ifndef BT_SOURCE_MOCK_H
#define BT_SOURCE_MOCK_H

#include "bt_source.h"
#include "esp_err.h"
#include "bt_mock.h" /* include the component mock header so BT_MOCK_PROVIDES_PROTOTYPES is defined
					  * before we check it — avoids static inline vs non-static prototype
					  * collisions when this header is included before bt_mock.h
					  * in a translation unit.
					  */

#ifdef __cplusplus
extern "C" {
#endif

void bt_source_mock_reset_impl(void);
static inline void bt_source_mock_reset(void) {
	bt_source_mock_reset_impl();
}
/* Legacy name: provide a prototype for bt_reset_for_test so callers get a
 * declaration. The real implementation may be the weak stub in
 * `bt_source_stubs.c` or a component-provided function; we avoid inlining a
 * definition to prevent redefinition conflicts across translation units.
 */
void bt_reset_for_test(void);
/* If the bt_mock component already provides external prototypes, don't
 * declare internal inline wrappers for the same symbols to avoid
 * conflicting linkage. The component header defines BT_MOCK_PROVIDES_PROTOTYPES
 * when it provides the canonical prototypes.
 */
#if !defined(BT_MOCK_PROVIDES_PROTOTYPES)
static inline void bt_mock_reset(void) { bt_source_mock_reset_impl(); }
#endif

/**
 * Simulate SSP pairing request
 * 
 * @param passkey Passkey to display (6-digit number)
 */
void bt_source_mock_simulate_ssp_request_impl(uint32_t passkey);
static inline void bt_source_mock_simulate_ssp_request(uint32_t passkey) {
	bt_source_mock_simulate_ssp_request_impl(passkey);
}
#if !defined(BT_MOCK_PROVIDES_PROTOTYPES)
static inline void bt_mock_simulate_ssp_request(uint32_t passkey) { bt_source_mock_simulate_ssp_request_impl(passkey); }
#endif

/**
 * Set whether SSP is supported
 * 
 * @param supported Whether SSP is supported
 */
void bt_source_mock_set_ssp_supported_impl(bool supported);
static inline void bt_source_mock_set_ssp_supported(bool supported) {
	bt_source_mock_set_ssp_supported_impl(supported);
}
#if !defined(BT_MOCK_PROVIDES_PROTOTYPES)
static inline void bt_mock_set_ssp_supported(bool supported) { bt_source_mock_set_ssp_supported_impl(supported); }
#endif

/**
 * Add a test device to the mock
 * 
 * @param addr_str Device address string
 * @param name Device name
 * @param type Device type
 */
void bt_mock_add_test_device(const char* addr_str, const char* name, bt_device_type_t type);

/**
 * Simulate PIN pairing failure
 */
void bt_source_mock_simulate_pin_failure_impl(void);
static inline void bt_source_mock_simulate_pin_failure(void) {
	bt_source_mock_simulate_pin_failure_impl();
}
#if !defined(BT_MOCK_PROVIDES_PROTOTYPES)
static inline void bt_mock_simulate_pin_failure(void) { bt_source_mock_simulate_pin_failure_impl(); }
#endif

/**
 * Simulate pairing timeout
 */
void bt_source_mock_simulate_pairing_timeout_impl(void);
static inline void bt_source_mock_simulate_pairing_timeout(void) {
	bt_source_mock_simulate_pairing_timeout_impl();
}
#if !defined(BT_MOCK_PROVIDES_PROTOTYPES)
static inline void bt_mock_simulate_pairing_timeout(void) { bt_source_mock_simulate_pairing_timeout_impl(); }
#endif

/**
 * Set discovered devices for the mock
 * 
 * @param devices Array of devices
 * @param count Number of devices
 */
void bt_mock_set_discovered_devices(const bt_device_t* devices, int count);

/**
 * Unpair all devices
 * 
 * @return ESP_OK if successful
 */
esp_err_t bt_unpair_all_devices(void);

/**
 * Get paired device count 
 *
 * @return Number of paired devices
 */
uint16_t bt_get_paired_device_count(void);

/**
 * Synchronize the mock's cached device list with the authoritative
 * component-level paired device entry so bt_is_device_paired() sees the
 * updated state.
 */
void bt_source_mock_cache_paired_device(const bt_device_t* device);

/**
 * Release the deferred disconnect visibility flag maintained by the mock so
 * stub helpers and Unity tests see the authoritative connected state.
 */
void bt_mock_release_disconnect_visibility(void);

#ifdef __cplusplus
}
#endif

#endif /* BT_SOURCE_MOCK_H */
