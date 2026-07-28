#pragma once

#include "bt_duplex_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Atomically update requested/effective mode for one authoritative session.
 * Mode changes are rejected while HFP audio or I2S resources are live. */
esp_err_t bt_duplex_set_mode(uint32_t generation,
                             const char *peer_mac,
                             bt_duplex_mode_t requested_mode);

#ifdef __cplusplus
}
#endif
