ESP32 Bluetooth audio source – production app overview
======================================================

This document explains how the production app under `main/` is structured, how audio flows through it, and which modules own key responsibilities. It focuses on the runtime logic (tasks, queues, state machines) rather than build setup.

What the app does
-----------------
- Acts as a Bluetooth Classic A2DP **source** with AVRCP controller to a paired speaker/headset.
- Captures PCM from I2S (or generates synthetic/diagnostic audio), converts/resamples, and pushes it through a shared ring buffer to the BT stack.
- Plays short beeps and WAV clips through the same pipeline so priority and volume handling remain consistent.
- Exposes a small command interface (UART-driven) for diagnostics and control.

Startup and system services
---------------------------
- `main.c` boots NVS, initializes the Bluetooth stack (GAP + A2DP source + AVRCP controller), registers callbacks, and creates a lightweight command-processing task that polls `cmd_process()`.
- A simple state machine tracks A2DP/AVRCP link state (idle → discovering → connecting → connected → streaming). A FreeRTOS timer (“heart beat”) drives retries and keep-alive behavior.
- Device name and peer name filters come from Kconfig (`CONFIG_EXAMPLE_PEER_DEVICE_NAME`).
- When the stack is up, the app hands audio buffers to the BT data callback; when disconnected, the audio pipeline can still run locally for diagnostics.

Audio pipeline (ring buffer core)
---------------------------------
- The pipeline centers on `audio_processor.c`, which owns task orchestration, buffer ownership, and source selection.
- `audio_ringbuffer.c` implements a single-producer/single-consumer ring buffer used by the audio engine task and the BT stack callback.
- Sources feeding the ring buffer:
	- **I2S capture** via `i2s_manager.c`: reads DMA frames, converts bit depth, and resamples to the configured output rate before filling the ring buffer.
	- **WAV playback** via `play_manager.c`: parses PCM WAV headers, converts/resamples frames into the ring, and tracks residual bytes between chunks to keep frame alignment.
	- **Beep generation** via `beep_manager.c`: synthesizes short sine beeps with fade-in/out and overlays them during reads.
	- **Synthetic tone/keepalive** via `synth_manager.c` (and helpers inside `audio_processor.c`): used when capture is unavailable or to keep the BT link alive.
- `audio_processor` produces into the ring buffer, applies volume/mute, tracks stats, and the BT A2DP data callback reads from the ring. It also exposes diagnostics (probe dumps, tag-miss counters, ring drain helpers) and runtime switches (force synth mode, DRAM-only allocations).

Bluetooth data path
-------------------
- In `main.c`, `bt_app_a2d_data_cb()` is the A2DP source data callback. It reads processed audio via `audio_processor_read()` and copies it into the buffer provided by the BT stack. When no audio is ready, the ring buffer path zero-fills to prevent stream stalls.
- AVRCP callbacks handle remote control notifications (e.g., volume change) and respond with capability queries and notifications when supported by the peer.
- GAP callback filters discovery results by name, caches the peer BDA, and triggers connects.

Tasks, timers, and concurrency
------------------------------
- Command task: polls UART for user commands at ~20 ms cadence.
- Audio processor task: owns the processing loop; manages prefill, draining, resample work buffers, and keepalive timing.
- I2S capture: pulled on demand by the audio engine loop via `i2s_source_fill()`, which reads DMA with short timeouts.
- Heart-beat timer: periodic state machine tick for BT connect/retry; also used to gate some diagnostics.
- Mutexes/critical sections: play/beep managers use mutexes or spinlocks around state transitions; ring buffer access is lock-free (SPSC design).

Configuration and data formats
------------------------------
- `audio_config_t` (see `include/audio_processor.h`) defines sample rate, bit depth (16/24/32), channel mode (mono/stereo), volume (0–100), mute flag, I2S port, and optional pin assignments. Pins can be updated at runtime via `audio_processor_set_i2s_pins()`; the processor restarts to apply changes.
- Default output format (A2DP payload): 16-bit, 44.1 kHz, stereo. See the boot-time init in [main.c](main/main.c#L966-L977). All sources (I2S capture, WAV, beep, synth) are converted/resampled to this configured output before entering the ring buffer.
- Producers (beep_manager, i2s_manager, play_manager, synth_manager) provide PCM in the current output format via source fill() functions; by default that is 16-bit, 44.1 kHz, stereo.
- `audio_stats_t` reports samples processed, buffer overruns/underruns, conversion errors, CPU load (approximate), and buffer levels.
- The ring buffer produces/consumes in 1024-byte chunks. Producers align chunk sizes to frame boundaries when possible (see `play_manager.c`).

WAV playback details (`play_manager.c`)
---------------------------------------
- Parses RIFF/WAVE headers, supports PCM only, validates format chunk, and locates the data chunk length.
- Tracks source format (bit depth, sample rate, channels) and converts to the configured output format.
- Maintains residual bytes so frame boundaries stay aligned across fill operations.
- Thread-safety via a mutex; `play_manager_is_active()` reports whether a file is in-flight.

Beep generation details (`beep_manager.c`)
------------------------------------------
- Generates sine samples with configurable duration, frequency, amplitude; clamps duration to 20 s, defaults to 50 ms at 1 kHz.
- Applies cosine fade-in/out (`BEEP_FADE_MS`) to avoid clicks, and mixes into the output stream via the overlay path.
- Tracks state (`STOPPED`/`PLAYING`) and supports a completion callback when the overlay finishes.

I2S capture details (`i2s_manager.c`)
--------------------------------------
- Configures an I2S RX channel as slave, sets DMA descriptors/counts, and assigns pins from `audio_config_t` (with `GPIO_NUM_NC` / `I2S_GPIO_UNUSED` fallbacks under mock builds).
- Reads into a caller-supplied raw buffer, converts bit depth, resamples to the output rate, and fills the ring buffer with capture audio.
- Mock/testing mode (`CONFIG_BT_MOCK_TESTING`) bypasses hardware by generating synthetic frames and uses a relaxed clock config.

Audio processor responsibilities (`audio_processor.c`)
-----------------------------------------------------
- Initializes shared buffers (capture, work, conversion) with optional DRAM-only mode for boards sensitive to PSRAM.
- Orchestrates sources: prioritizes beeps and WAV playback, otherwise uses live I2S capture; falls back to synthetic tones/keepalive when no capture data is available or when A2DP is disconnected.
- Applies volume/mute to produced audio, maintains residual ring-buffer bytes, and tracks tag continuity. Provides stats via `audio_processor_get_stats()` and status via `audio_processor_get_status()`.
- Exposes diagnostics: probe arm/emit for I2S timing, sync worker dump, keepalive arming, and ring drain helpers.
- Public helpers: `audio_processor_beep[_tone]`, `audio_processor_play_wav`, `audio_processor_read`, `audio_processor_set_synth_mode`, `audio_processor_set_dram_only`, and test-only injection APIs under `CONFIG_BT_MOCK_TESTING`.

Bluetooth control helpers
-------------------------
- `bt_app_core.c`, `bt_connection_manager.c`, and `bt_streaming_manager.c` wrap command/streaming logic around the ESP-IDF A2DP/AVRCP APIs (event dispatch, command queueing, discovery filters, AVRCP pass-through handling). `bt_source_component.c` provides component registration glue.

Diagnostics and testing
-----------------------
- Host/unit tests target the pure C logic with mocks; mock builds replace hardware with synthetic data paths (`CONFIG_BT_MOCK_TESTING`).
- Diagnostic commands can trigger ring buffer drains, beep diagnostics, I2S probes, and WAV playback from SPIFFS (`/spiffs/*.wav`).
- Logs use consistent module tags (AUDIO_PROC, i2s_manager, play_manager, beep_manager, BT_AV, RC_CT). Warnings/errors include esp_err_t codes; `ESP_RETURN_ON_ERROR` guards most hardware calls.

Operational notes
-----------------
- Ensure the ring buffer initializes successfully; producers/consumers bail if it is absent.
- If PSRAM artifacts appear, call `audio_processor_set_dram_only(true)` before init to force DRAM allocations.
- When integrating new sources, fill the ring buffer with properly formatted PCM and supply meaningful `audio_source_tag_t` values so stats and diagnostics remain accurate.

Diagrams
--------

Audio pipeline (source selection and flow)
```
	    +------------------+
	    |  Command inputs  |
	    | (UART/CLI/tests) |
	    +---------+--------+
		      |
		      v
	       +--------------+
	       | audio_proc   |<-------------------------+
	       | coordinator  |                          |
	       +------+-------+                          |
		      |                                   |
	  source select order                             |
	  (beep > WAV > I2S > synth)                      |
		      v                                   |
    +-----------------+-----------------+                 |
    |  Beep manager   |  WAV play mgr   |  I2S manager    |
    |  (tone synth)   |  (file decode)  |  (capture DMA)  |
    +--------+--------+--------+--------+--------+-------+
	     |                 |                 |
	     v                 v                 v
	      +------------------------+
	      | audio_proc coordinator |
	      | (mix, volume/mute)     |
	      +-----------+------------+
			  |
			  v
	+------------------------+
	| audio_ringbuffer       |
	| (1 KiB producer chunks)|
	+-----------+------------+
		    |
		    v
	    +---------------+
	    | BT A2DP data  |
	    | callback      |
	    +---------------+
```

Bluetooth link state machine (main.c)
```
	+-----+      discover      +--------------+
	|Idle | ------------------> | Discovering  |
	+--+--+                     +------+-------+
	   ^                              |
	   |                              | found peer
	   |                              v
	   |                       +--------------+
	   |           connect     | Discovered   |
	   | <---------------------+--------------+
	   |                              |
	   |                              | start connect
	   |                              v
	+--+--+     connected      +--------------+     start stream     +-----------+
	|Unconn| <----------------- | Connecting  | --------------------> | Connected |
	+--+--+                      +------+-------+                      +-----+-----+
	   ^                               |                                     |
	   |                               | disconnect                           |
	   |                               v                                     v
	   |                         +--------------+                     +-------------+
	   |                         | Disconnecting| ------------------> | Media start |
	   |                         +------+-------+   stop stream       +------+------+ 
	   |                                |                                 |
	   |                                | done/failed                     |
	   +--------------------------------+---------------------------------+

Media sub-state (inside Connected)
    IDLE -> STARTING -> STARTED -> STOPPING -> IDLE
```
