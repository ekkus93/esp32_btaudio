#ifndef SYNTH_MANAGER_H_
#define SYNTH_MANAGER_H_

#include <stddef.h>
#include <stdbool.h>
#include "audio_processor.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
typedef portMUX_TYPE synth_lock_t;
#else
typedef void synth_lock_t;
#endif

/**
 * Synth source fill API for ring buffer architecture (CODE_REVIEW6 Phase 3.4)
 * 
 * WHY: Ring buffer needs direct fill (not enqueue) for synth source
 * HOW: Wraps existing synth_manager_generate_audio(), generates 20kHz tone with fade
 * CORRECTNESS: Stateless generation (no locking needed), returns bytes written
 * 
 * @param dst Destination buffer (must be large enough for dst_bytes)
 * @param dst_bytes Requested bytes to generate
 * @return Bytes written (0 if error, dst_bytes on success)
 */
size_t synth_source_fill(uint8_t *dst, size_t dst_bytes);

/**
 * Check if synth source is active
 * 
 * @return true if synth envelope active (fade in/out), false otherwise
 */
bool synth_source_is_active(void);

/**
 * Reset synth state (phase, envelope, fade)
 */
void synth_manager_reset_state(void);

/**
 * Legacy synth generation API (CODE_REVIEW6 Phase 6: REMOVE)
 * Use synth_source_fill() instead for ring buffer architecture
 */
size_t synth_manager_generate_audio(uint8_t* buffer,
									size_t buffer_size,
									const audio_config_t* config,
									bool *force_synth_flag,
									synth_lock_t *lock);

#endif // SYNTH_MANAGER_H_
