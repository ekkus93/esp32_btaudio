#ifndef CMD_HANDLERS_HFP_INTERNAL_H
#define CMD_HANDLERS_HFP_INTERNAL_H

/* Internal (non-public) helpers shared between the HFP command-handler
 * translation units (cmd_handlers_hfp_fd11_v2.c, cmd_handlers_hfp_wire.c,
 * cmd_handlers_hfp_stats.c). Not part of the command_interface public API —
 * do not include from outside this component. */

#include <stddef.h>

#include "bt_hfp_manager.h"

#define HFP_DATA_BUFFER_SIZE 320U

/* Wire-format enum-to-string helpers (cmd_handlers_hfp_wire.c) */
const char *wire_mode(bt_duplex_mode_t value);
const char *wire_a2dp_profile(bt_a2dp_profile_state_t value);
const char *wire_a2dp_audio(bt_a2dp_audio_state_t value);
const char *wire_hfp_profile(bt_hfp_profile_state_t value);
const char *wire_hfp_audio(bt_hfp_audio_state_t value);
const char *wire_codec(bt_hfp_codec_t value);
const char *wire_i2s(bt_hfp_i2s_state_t value);
const char *wire_health(bt_audio_health_t value);
const char *wire_policy_state(bt_duplex_policy_state_t value);
const char *wire_policy_reason(bt_duplex_policy_reason_t value);
const char *wire_downlink_owner(bt_duplex_downlink_owner_t value);

/* Shared formatting helpers (cmd_handlers_hfp_fd11_v2.c) */
void sanitize_field(const char *source, char *out, size_t out_size);
bool format_checked(char *out, size_t out_size, const char *format, ...);

/* Stats/diagnostics wire formatting (cmd_handlers_hfp_stats.c) */
bool send_stats_lines(const bt_hfp_manager_stats_t *stats);
bool send_diagnostics_lines(const bt_hfp_manager_diagnostics_t *diagnostics);

#endif /* CMD_HANDLERS_HFP_INTERNAL_H */
