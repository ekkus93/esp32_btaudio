#ifndef CMD_HANDLERS_H
#define CMD_HANDLERS_H

#include "command_interface.h"
#include "commands_priv.h"

cmd_status_t cmd_handle_status(const cmd_context_t *ctx);
cmd_status_t cmd_handle_mem(const cmd_context_t *ctx);
cmd_status_t cmd_handle_version(const cmd_context_t *ctx);
cmd_status_t cmd_handle_reset(const cmd_context_t *ctx);
cmd_status_t cmd_handle_help(const cmd_context_t *ctx);

cmd_status_t cmd_handle_scan(const cmd_context_t *ctx);
cmd_status_t cmd_handle_synth(const cmd_context_t *ctx);
cmd_status_t cmd_handle_diag(const cmd_context_t *ctx);
cmd_status_t cmd_handle_beep(const cmd_context_t *ctx);
cmd_status_t cmd_handle_start(const cmd_context_t *ctx);
cmd_status_t cmd_handle_stop(const cmd_context_t *ctx);
cmd_status_t cmd_handle_play(const cmd_context_t *ctx);
cmd_status_t cmd_handle_i2s_config(const cmd_context_t *ctx);
cmd_status_t cmd_handle_sample_rate(const cmd_context_t *ctx);
cmd_status_t cmd_handle_volume(const cmd_context_t *ctx);
cmd_status_t cmd_handle_mute(const cmd_context_t *ctx);
cmd_status_t cmd_handle_unmute(const cmd_context_t *ctx);

cmd_status_t cmd_handle_file(const cmd_context_t *ctx);
cmd_status_t cmd_handle_files(const cmd_context_t *ctx);
cmd_status_t cmd_handle_parts(const cmd_context_t *ctx);

cmd_status_t cmd_handle_connect(const cmd_context_t *ctx);
cmd_status_t cmd_handle_connect_name(const cmd_context_t *ctx);
cmd_status_t cmd_handle_disconnect(const cmd_context_t *ctx);
cmd_status_t cmd_handle_pair(const cmd_context_t *ctx);
cmd_status_t cmd_handle_paired(const cmd_context_t *ctx);
cmd_status_t cmd_handle_confirm_pin(const cmd_context_t *ctx);
cmd_status_t cmd_handle_enter_pin(const cmd_context_t *ctx);
cmd_status_t cmd_handle_set_default_pin(const cmd_context_t *ctx);
cmd_status_t cmd_handle_unpair(const cmd_context_t *ctx);
cmd_status_t cmd_handle_unpair_all(const cmd_context_t *ctx);
cmd_status_t cmd_handle_set_name(const cmd_context_t *ctx);
cmd_status_t cmd_handle_debug(const cmd_context_t *ctx);

#endif /* CMD_HANDLERS_H */
