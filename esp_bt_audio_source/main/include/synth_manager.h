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

void synth_manager_reset_state(void);
size_t synth_manager_generate_audio(uint8_t* buffer,
									size_t buffer_size,
									const audio_config_t* config,
									bool *force_synth_flag,
									synth_lock_t *lock);

#endif // SYNTH_MANAGER_H_
