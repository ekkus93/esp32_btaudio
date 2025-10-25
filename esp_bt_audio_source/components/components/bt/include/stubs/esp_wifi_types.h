/* esp_wifi_types.h shim for Bluetooth-only builds.
 * When the full Wi-Fi component is disabled, reuse the generic
 * type definitions directly so BLUFI can compile without discrepancies.
 */

#ifndef __ESP_WIFI_TYPES_STUB_H__
#define __ESP_WIFI_TYPES_STUB_H__

#include "../../../esp_wifi/include/esp_wifi_types_generic.h"

#endif /* __ESP_WIFI_TYPES_STUB_H__ */
