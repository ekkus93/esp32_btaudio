#ifndef CMD_HANDLERS_INTERNAL_H
#define CMD_HANDLERS_INTERNAL_H

/* Internal (non-public) state shared between the command-handler translation
 * units.  Not part of the command_interface public API — do not include from
 * outside this component.
 *
 * Mock pairing state for host-test/debug pairing simulation.  Defined once in
 * cmd_handlers_bt.c; also written by the DEBUG subcommand handlers in
 * cmd_handlers_debug.c.
 *
 * Access contract: on the device, commands arrive serially from a single UART
 * task, so no locking is required.  Host unit tests run single-threaded.  If
 * this changes (e.g., concurrent command sources are added), protect these with
 * a mutex. */

#include <stdbool.h>

extern bool g_cmd_mock_enabled;
extern char g_cmd_mock_pairing_addr[32];
extern char g_cmd_mock_passkey[16];

#endif /* CMD_HANDLERS_INTERNAL_H */
