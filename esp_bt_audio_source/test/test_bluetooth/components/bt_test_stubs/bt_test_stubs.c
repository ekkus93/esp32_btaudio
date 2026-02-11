/*
 * Test-only stubs for symbols pulled by the command_interface and bt_manager
 * when building the device-side Unity images. These are minimal, header-free
 * implementations so the test images can be built without exposing the full
 * production component include tree.
 */

#include <stdbool.h>
#include <stdint.h>

/* Allow these stubs to be overridden by the real app when it is linked
 * into the test image (mark as weak so the strong symbol wins).
 */
#if defined(__GNUC__)
#define WEAK __attribute__((weak))
#else
#define WEAK
#endif

/* Return a conservative default: not streaming. Mark weak so the production
 * `main` implementation can supply a strong symbol and win at link-time.
 */
WEAK int bt_get_streaming_state_int(void)
{
    return 0;
}

/* Connection state callback stub: no-op for tests. */
WEAK void bt_connection_state_cb(int state, const void *bd_addr)
{
    (void)state; (void)bd_addr;
}

/* Audio state callback stub: no-op for tests. */
WEAK void bt_audio_state_cb(int state, const void *bd_addr)
{
    (void)state; (void)bd_addr;
}

/* Allow the command interface to toggle synth mode in tests; no-op here. */
WEAK void audio_processor_set_synth_mode(bool enable)
{
    (void)enable;
}
