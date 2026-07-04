/**
 * uart_audio.h — UARTAUDIO streaming mode (laptop -> ESP32 PCM over UART0)
 *
 * Text-mode side of the feature: the UARTAUDIO command handler and the
 * streaming-mode flag that hands UART RX ownership from cmd_process()
 * to the dedicated reader task.
 *
 * Handoff contract (race-free):
 *   1. cmd_process() checks uart_audio_is_streaming() at the very top of
 *      each cycle and returns without touching the UART when set.
 *   2. The START handler runs after the cycle's single uart_read_bytes
 *      already happened, queues OK|UARTAUDIO|STARTING, THEN sets the
 *      flag (uart_audio_begin) and spawns the reader task, whose first
 *      read strictly follows the flag set.
 *   3. On exit the reader restores 115200 baud, flushes RX and clears
 *      the flag as its last action — never two concurrent UART readers.
 */

#ifndef UART_AUDIO_H
#define UART_AUDIO_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Nominal DATA frame payload (see uart_audio_frame.h wire format). */
#define UART_AUDIO_FRAME_PAYLOAD_BYTES 1024U

/* Kconfig-provided on device (UARTAUDIO-7); fallbacks keep host builds
 * and pre-Kconfig device builds working. */
#ifndef CONFIG_UART_AUDIO_STREAM_BAUD
#define CONFIG_UART_AUDIO_STREAM_BAUD 921600
#endif
#ifndef CONFIG_UART_AUDIO_STAGING_RB_KB
#define CONFIG_UART_AUDIO_STAGING_RB_KB 32
#endif

/** True while the UART reader task owns UART RX (binary streaming mode). */
bool uart_audio_is_streaming(void);

/**
 * Hand over the UART driver event queue created by main.c's
 * uart_driver_install (QueueHandle_t passed as void* to keep this header
 * platform-neutral). The streaming reader task drains it to count
 * UART_FIFO_OVF / UART_BUFFER_FULL / frame errors — the discriminator
 * between ISR starvation, reader starvation and wire corruption.
 * No-op on host builds; safe to never call (counters just read 0).
 */
void uart_audio_set_event_queue(void *queue);

/**
 * Enter streaming mode: sets the streaming flag and (device build,
 * UARTAUDIO-6) spawns the reader task that switches baud and consumes
 * frames. Returns 0 on success, -1 if already streaming.
 * Call only after the STARTING response has been queued.
 */
int uart_audio_begin(int baud);

#if defined(UNIT_TEST)
/** Test hook: force streaming mode off and reset internal state. */
void uart_audio_test_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* UART_AUDIO_H */
