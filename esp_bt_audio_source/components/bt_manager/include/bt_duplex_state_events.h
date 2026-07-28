#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Records an event that belongs to an expired or superseded operation.
 * The peer and generation are still validated before the counter changes. */
esp_err_t bt_duplex_record_stale_operation_event(
    uint32_t generation,
    const char *peer_mac);

#ifdef __cplusplus
}
#endif
