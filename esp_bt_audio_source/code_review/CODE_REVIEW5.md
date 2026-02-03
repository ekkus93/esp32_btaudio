This is the latest version of esp_bt_audio_source. Can you review the code? Again, tell me what's good about the code and what's bad about the code? Is there anything that needs to be fixed or needs improvement?

I reviewed the esp_bt_audio_source_revised02021646.zip version (the one you most recently uploaded). Below is (1) what the code does, (2) what’s notably good, (3) what’s still weak or risky, and (4) a focused deep-dive on WAV playback potentially ending early (including why your “stops early” suspicion can still be true even if “no bytes are lost”).

What this firmware does (as implemented now)

At a high level, this firmware is an A2DP Source that streams audio to a Bluetooth speaker/headphones, with a control plane over UART.

main/main.c acts as a bootstrap/policy layer:

Early boot diagnostics markers (DIAG|...)

Initializes UART early so the command layer can work immediately

Initializes NVS once, loads config (Kconfig defaults + NVS overrides)

Initializes:

command_interface (UART command parser + handlers)

bt_manager (Bluetooth lifecycle + A2DP/AVRCP/GAP logic)

audio_processor (audio routing + WAV/beep/I2S behavior)

Applies autostart policy (NVS overrides Kconfig)

bt_manager owns the BT stack and streaming:

The A2DP source data callback pulls audio by calling audio_processor_read(...)

When not streaming/paused it returns silence (zero-fill)

audio_processor is the audio “media plane”:

Maintains an audio queue of chunks tagged by source (WAV, beep, I2S, synth/keepalive)

Exposes audio_processor_read() (consumed by BT)

Handles PLAY commands: mounts SPIFFS (on target), opens WAV, enqueues audio via play_manager

Has logic to prevent mixing beep/WAV/I2S in ways that conflict

play_manager is your WAV ingestion pipeline:

Parse WAV header → read PCM blocks → format convert → resample → enqueue chunks

Handles backpressure (queue full / OOM) with “rewind and retry” so bytes aren’t silently skipped

What’s good in the current code
1) The architectural separation is much cleaner now

You’ve largely achieved the “thin main.c” goal:

main.c doesn’t directly implement BT state machines or call deep A2DP APIs; it delegates.

The boundaries between bootstrap (main), control plane (command_interface), BT lifecycle (bt_manager), media plane (audio_processor/play_manager) are real and enforced by structure.

2) The WAV “data-loss” class of bugs you were fighting looks addressed

The big correctness fixes I see in this version (and they matter):

Rewind on enqueue failure (play_manager.c: rewind_after_enqueue_failure()):

This is the right fix for “queue full / OOM causes file pointer to advance and skip audio”.

Without this, WAV playback can truncate even if the file read loop is “correct”.

Frame-boundary alignment order (calculate_read_size(...)):

The order “clamp to remaining bytes, then align down to frame size” is the correct order.

This prevents partial frames at EOF and prevents subtle corruption/glitches.

Residual handling now avoids an early-return bug (audio_processor_read.c):

You now guard against returning “no WAV active” while residual bytes remain.

That was a classic “tail gets dropped” failure mode.

These three together eliminate the most common true truncation scenarios.

3) You’re building real testability hooks and diagnostics

Early boot markers + DIAG prints are consistent and machine-parseable.

“host-mode” paths in the command layer indicate you’re thinking about unit tests and non-target environments.

The code is increasingly defensive about initialization order and invalid state.

What’s bad / needs improvement (design + implementation)
A) Your resampler is almost certainly capable of making playback “end early”

This is the biggest “you’re not crazy” point.

Your resample_audio() implementation is block-local and uses:

dst_frame_count = floor(src_frame_count * ratio) (per chunk)

it does not carry fractional phase across chunks

That alone can shorten total duration (cumulative rounding loss).

But there’s a more serious issue:

Upsampling is effectively capped by work_bytes

In resample_audio():

max_dst_frames = work_bytes / frame_bytes

In practice your work_bytes is often tied to the source block size / chunk size budget.

For ratios > 1 (e.g., 44.1k → 48k), you need more output frames than input frames.

Your resampler caps output to the same memory budget, which can force:

fewer output frames than mathematically required

shorter playback duration (and often wrong pitch/speed depending on the rest of the pipeline)

So even if you read and enqueue everything, you can still perceive “stopping early” because you simply didn’t produce enough output samples.

This is consistent with your symptom.

✅ Actionable fix direction (choose one):

Hard requirement: reject WAV files whose sample rate != A2DP output sample rate, and don’t resample at all.

simplest, most reliable

Streaming resampler: keep a phase accumulator / fractional index across blocks (stateful resampling).

correct duration

Change the pipeline contract: make output chunk size fixed, compute required input bytes per fill based on ratio, and read variable input sizes.

more involved but works well with fixed-size output queue

If you want “it just works,” #1 is the fastest and most robust; #2/#3 are the “real solution.”

B) The WAV “data loss” instrumentation is currently misleading

In play_manager.c, the completion report compares:

Bytes read from file

Bytes enqueued

and labels differences as “data loss”.

That’s wrong whenever conversion/resampling changes byte counts (which it will).

Downsampling legitimately enqueues fewer bytes than read.

Upsampling should enqueue more bytes than read (but you may not due to the cap above).

✅ Improve instrumentation:

Track PCM frames not bytes.

Report:

src_pcm_frames_consumed

dst_pcm_frames_produced

expected_dst_frames (derived from header duration * output rate)

If you still want a “loss” metric:

define it in output frames, not input bytes.

C) bt_streaming_manager stats are wrong in a way that hides underflows

In the A2DP callback:

you always do s_streaming_info.bytes_sent += len;

even if bytes_read < len and you zero-fill remainder

So your “bytes sent” counter counts silence as successfully streamed audio.

✅ Fix:

track bytes_produced separately from bytes_requested

expose underrun rate; you already log underruns, but the stats should reflect them

D) Error handling and return codes are inconsistent across modules

You mix:

esp_err_t style returns

bt_err_t typedefs (that appear to be int-like)

legacy enums still in headers “for compatibility”

This makes it harder to reason about correctness and introduces silent mismatches.

✅ Recommendation:

Standardize on esp_err_t at boundaries and convert internally if needed.

If you need a domain-specific status, wrap it in a struct:

{ esp_err_t err; bt_state_t state; ... }

E) The repo layout includes a huge components/components tree

This looks like a vendored ESP-IDF component tree. If it’s intentional (host tests, pinning IDF, etc.), it needs strong documentation because it’s unusual and can:

confuse build/link resolution

bloat the repo

make it unclear what is “your code” vs “IDF”

✅ If it’s for tests:

isolate it under third_party/ or vendor/

document exactly how it is used and why

WAV playback: why it might “stop early” and how to prove it
Likely root cause: resampling output duration drift

If your WAV is not already at the same sample rate as the A2DP output config, the current resampler can shorten the output.

Quick ways to confirm:

Play a WAV that exactly matches output settings

16-bit stereo PCM at the configured A2DP sample rate (likely 44.1k or 48k)

If that plays full length reliably, your queue/rewind/residual fixes are working, and resampling is the culprit.

Log frame counts
Add a one-line diagnostic in play_manager:

total source frames read (from WAV header / bytes read)

total output frames enqueued
Compare:

dst_frames_enqueued should approximately equal src_frames * dst_rate/src_rate
If it’s consistently lower → your output is shorter.

Disable resampling temporarily
Force src_rate == dst_rate (or early-reject) and see if symptom disappears.

Secondary causes to keep in mind

Even with perfect WAV handling, playback can appear to stop early if:

A2DP stream state changes (pause/stop/disconnect) → callback returns silence

Queue underruns → you log underruns and zero-fill, which can sound like playback ended

Keepalive/synth policy kicks in after WAV completes and you interpret silence as “stopped” (less likely, but possible depending on your speaker behavior)

Concrete “next improvements” I’d prioritize

Decide the WAV sample-rate policy

If you can constrain WAV assets: reject mismatched sample rate and remove resampling (fast win).

If you need arbitrary WAV: implement a stateful streaming resampler.

Fix WAV completion instrumentation

stop calling resampling byte differences “data loss”

report frame-domain metrics

Strengthen underflow observability

stats should reflect underruns, not just logs

expose a command to dump audio queue + wav state + underrun counters

Tighten API contracts

unify return codes

keep legacy BT enums out of new code paths

If you tell me:

the A2DP output sample rate you’re using (44.1k vs 48k), and

the WAV sample rate/format you’re testing with,

…I can point to the exact path where your current resampler will shorten playback and recommend the minimal patch (either “reject mismatched WAV” or a small stateful-resample implementation that fits your chunking model).

For 'A) Your resampler is almost certainly capable of making playback “end early”', can you go into more details about what's involved with: Change the pipeline contract: make output chunk size fixed, compute required input bytes per fill based on ratio, and read variable input sizes.

Sure. “Change the pipeline contract” means you stop thinking of playback as “read N bytes from the WAV file, then resample them,” and instead think of it as “the downstream consumer needs exactly K output frames per fill, so I must pull whatever number of input frames are required to produce K output frames (given the ratio), and I must carry fractional phase across fills.”

This solves the two big problems you’re hitting today:

Upsampling can’t fit in a fixed work_bytes buffer if you read a fixed input block (you need more output than input).

Block-local rounding causes cumulative frame loss (shorter playback) if you don’t keep phase.

Below is a concrete design for that contract, including formulas, buffering, and exactly what to change in your play_manager_fill() style code.

1) Define the new contract
Old contract (what you likely have now)

Producer reads a fixed input block size (e.g., 1024 bytes)

Converts/resamples within that block

Enqueues “whatever output comes out”

This is fragile for upsampling and for accurate duration.

New contract (fixed output, variable input)

Producer must enqueue exactly OUT_FRAMES_PER_CHUNK frames each time it successfully produces audio

Producer reads as many input frames as needed to produce that many output frames, based on the resampling ratio, plus a small safety margin for filter/interpolation and phase.

That implies:

Output chunk is fixed size (predictable for your queue and BT callback)

Input read size varies depending on:

src_rate, dst_rate

current phase (fractional position)

2) Pick a fixed output chunk size

Define:

OUT_FRAMES_PER_CHUNK (e.g., 256 or 512 frames)

dst_frame_bytes = dst_channels * (dst_bits_per_sample / 8)

Then:

OUT_BYTES = OUT_FRAMES_PER_CHUNK * dst_frame_bytes

Example: stereo 16-bit (dst_frame_bytes = 4)

If OUT_FRAMES_PER_CHUNK = 512 → OUT_BYTES = 2048

This maps nicely to your 1 KiB pool blocks if you allow 2 blocks per chunk or set OUT_FRAMES_PER_CHUNK=256 to fit in 1 KiB.

Important: choose a chunk size that your audio_queue can carry without fragmentation. If your pool block is 1024 bytes and each queue element is a single block, then:

stereo 16-bit: max frames/block = 1024 / 4 = 256 frames
So 256 frames is a sweet spot.

3) Compute required input frames for a desired output chunk

Let:

src_rate = WAV sample rate (e.g., 44100)

dst_rate = BT output sample rate (e.g., 48000)

ratio = src_rate / dst_rate (input frames per output frame)

If you want N_out output frames, you need approximately:

N_in ≈ N_out * ratio

But because you’re stepping through input with a fractional index, you need:

N_in = ceil((phase + N_out) * ratio) - floor(phase * ratio) + GUARD

That’s overly formal; in practice you can do:

N_in = ceil(N_out * ratio) + 2 for linear interpolation

+ filter_taps if you use a FIR filter (you probably are not)

For linear interpolation, +2 is typically enough.

Example: 44.1k → 48k upsample

ratio = 44100/48000 = 0.91875

N_out = 256

N_in ≈ ceil(256 * 0.91875) + 2 = ceil(235.2) + 2 = 238

Input bytes to read:

IN_BYTES = N_in * src_frame_bytes

If WAV is stereo 16-bit, src_frame_bytes = 4

IN_BYTES = 238 * 4 = 952 bytes (fits in one 1 KiB src block)

Example: 48k → 44.1k downsample

ratio = 48000/44100 = 1.088435...

N_out = 256

N_in ≈ ceil(256 * 1.0884) + 2 = 282 + 2 = 284

IN_BYTES = 1136 bytes (might require 2 src blocks)

So for downsample you may read more input than output. That’s okay: input is variable.

4) Maintain a stateful resampler phase across fills

This is the key missing piece if you want correct duration.

Keep in play_manager resampler state:

double pos_in (or fixed-point) = current position in input frames

or uint32_t phase_fp in Q16.16 or Q8.24 fixed-point

For linear interpolation resampling:

For each output frame j = 0..N_out-1:

in_pos = pos_in + j * ratio

i0 = floor(in_pos)

frac = in_pos - i0

y = (1-frac)*x[i0] + frac*x[i0+1]

After producing the chunk:

pos_in += N_out * ratio

Then subtract the number of whole input frames you consumed from the front of your input buffer, leaving a small “tail” (at least 1 frame) for interpolation continuity.

Why this prevents “ends early”

Because you don’t “round down output frames per block.” You always output exactly N_out until you truly hit EOF, and the phase ensures you sample the correct input positions over time.

5) Introduce an input ring buffer (or “stash”) between file reads and resampling

Variable input read sizes are easiest if you maintain a small PCM ring buffer in source format after conversion to a common type.

Common approach:

Always convert source samples to int16 stereo (or float) in an internal ring buffer.

Resampler reads from ring buffer.

Data structures:

int16_t in_ring[IN_RING_FRAMES * channels]

size_t in_ring_frames_available

size_t in_ring_read_index

Each fill:

Ensure ring has at least N_in_required + 1 frames (for interpolation).

If not, read more from file into a temp src block, decode/convert, append to ring.

Produce exactly N_out frames into output block using the resampler state.

Advance ring read index based on frames truly consumed.

Ring size can be small: a few thousand frames is fine.

6) Backpressure behavior becomes clean and lossless

Because output chunk size is fixed, you can enforce:

Don’t read from file unless you can allocate/enqueue the output block (same as you fixed earlier).

If enqueue fails, you do not advance pos_in or ring indexes. No “rewind file” is needed if you only read when you can commit.

Even better: you can decouple file read from enqueue:

If queue is full, just return and retry later; the ring buffer retains the state.

7) Handling EOF correctly (the last chunk)

EOF is tricky because you may not have enough input frames to produce a full N_out output chunk.

Options:

Pad with silence for the last chunk and still enqueue a full output chunk

keeps A2DP callback smooth

duration stays correct; tail is silent for the final partial chunk

Enqueue a shorter last chunk

requires downstream to accept variable chunk sizes cleanly

your system already supports variable chunk sizes in queue, but BT callback requests fixed len and you have residual handling—so this can work too

I’d recommend (1) for simplicity and smoothness:

if you have fewer than needed input frames, generate remaining output frames as 0 and mark playback complete after enqueue.

8) What code changes this implies in your project

Translating to your modules:

play_manager_fill()

Becomes “produce up to X output chunks” where each output chunk is fixed frames:

Pseudo:

If !wav_active: return

While queue has room:

Allocate output block (fixed OUT_BYTES). If not available → return.

Ensure input ring has enough frames for this output block:

read variable bytes from file into temp block, append to ring

stop if EOF and not enough frames

Resample from ring → fill output block, potentially padding silence at EOF

Enqueue output block

If EOF and ring drained enough → mark playback done, close file, break

Input read sizing

Compute required frames for one output chunk:

N_in_need = ceil(N_out * src_rate / dst_rate) + 2

IN_BYTES = N_in_need * src_frame_bytes

clamp IN_BYTES to remaining bytes in WAV

Read enough to satisfy ring availability. This can mean multiple reads if your src block is fixed 1024 bytes.

Resampler

Replace your current resample_audio(src, dst, ratio) that operates per block with:

a stateful resampler that consumes from ring and produces exactly N_out frames.

9) Why this is the “right” contract for A2DP

A2DP callback is effectively a real-time consumer that wants steady output pacing. Fixed output chunk sizes:

match queue design (bounded memory, predictable fill)

simplify underrun behavior

simplify stats (frames produced vs requested)

eliminate the upsampling “buffer too small” trap

eliminate rounding drift across chunks

10) Practical sizing recommendations for your setup

Given your pool blocks are 1024 bytes:

If output is stereo 16-bit PCM: choose OUT_FRAMES_PER_CHUNK = 256 (exactly 1024 bytes)

Ring buffer: IN_RING_FRAMES = 2048 (stereo → 4096 samples), ~8KB for int16, very reasonable

Read block: keep your existing 1KB file read blocks; just loop reads to fill ring as needed

Summary

Changing the pipeline contract means:

Output is king: always produce fixed-size output chunks to match BT pacing.

Input is elastic: read whatever number of input bytes are needed to produce those fixed chunks.

Resampler is stateful: carry fractional phase across chunks so total duration is correct.

Backpressure is safe: don’t advance file/ring/resampler state unless you can enqueue.

If you want, I can sketch the exact structs and function signatures that fit your code style (e.g., resampler_state_t, wav_ring_t) and show how to retrofit them into play_manager.c with minimal churn.

Sure.

Cool — here’s a concrete, minimal-churn retrofit plan that matches how your current play_manager.c and audio_queue work today, but flips the contract to:

fixed output chunk size (exactly one AUDIO_CHUNK_BLOCK_BYTES block per enqueue), and

variable input read size (read however many source frames are needed to produce that output, based on the resample ratio),

with a stateful (streaming) resampler so you don’t “lose time” to per-block rounding.

Explain-first, then I’ll give you the structs + function signatures you can drop into your tree.

Why this contract fixes “ends early”

Your current audio_util.c::resample_audio():

computes dst_frame_count = floor(src_frame_count * ratio) per block

caps output by work_bytes / frame_bytes

has no phase carry across blocks

So even if you “read all bytes” and never drop blocks, you can still end up producing too few output frames overall (duration shrink), especially when upsampling (44.1k → 48k).

The contract change makes the output deterministic and forces the pipeline to pull enough input to supply that output, while a stateful resampler ensures the ratio is met over the whole file (not per block).

The minimal-churn design that fits your current code
Keep these pieces as-is

audio_queue block pool (1 KiB blocks)

play_manager_fill() loop style (bounded iterations, stop on backpressure)

convert_audio_format() for bit depth conversion

file parsing and “lossless rewind” for enqueue backpressure (still useful)

Add two small utilities

a PCM stash (a small “ring” / stash buffer of converted PCM frames)

a streaming resampler (phase accumulator; consumes from stash, produces a fixed number of output frames)

Key policy choices

Output chunk size: always AUDIO_CHUNK_BLOCK_BYTES bytes (1 pool block)

Output frames per chunk:

out_frames = AUDIO_CHUNK_BLOCK_BYTES / frame_bytes_dst;

Input format in stash: already converted to output bit depth and output channel count, but still at src_rate.
That avoids mixed channel math inside the resampler.

Important note for your current code: in play_manager.c, you currently pass s_pm.out_cfg.channels into resample_audio(), even when the WAV is mono. That is wrong unless you only ever play stereo WAVs. The stash approach lets you fix that cleanly by upmixing mono→stereo when appending to the stash.

New structs (drop-in)
1) Streaming resampler state

Create: components/audio_processor/include/audio_resampler_stream.h

#pragma once
#include <stddef.h>
#include <stdint.h>
#include "audio_processor.h"   // for audio_sample_rate_t, audio_bit_depth_t, audio_channel_t

typedef struct {
    audio_sample_rate_t src_rate;
    audio_sample_rate_t dst_rate;
    audio_bit_depth_t bit_depth;
    int channels;              // 1 or 2

    // Q16.16 fixed-point position in input frames
    uint32_t pos_q16;

    // Q16.16 step = (src_rate / dst_rate) in input-frames per output-frame
    uint32_t step_q16;
} audio_resampler_stream_t;

void audio_resampler_stream_init(audio_resampler_stream_t *st,
                                audio_sample_rate_t src_rate,
                                audio_sample_rate_t dst_rate,
                                audio_bit_depth_t bit_depth,
                                audio_channel_t channels);

// Minimum input frames required to produce out_frames (includes +1 for interpolation)
size_t audio_resampler_stream_min_in_frames(const audio_resampler_stream_t *st,
                                           size_t out_frames);

// Produce exactly out_frames (unless eof/pad policy says otherwise).
// Returns number of output frames written; sets *in_frames_consumed to whole frames
// that can be dropped from the stash (pos_q16 is updated and normalized).
size_t audio_resampler_stream_process(audio_resampler_stream_t *st,
                                     const uint8_t *in_pcm, size_t in_frames_avail,
                                     uint8_t *out_pcm, size_t out_frames,
                                     size_t frame_bytes,
                                     size_t *in_frames_consumed);

Create: components/audio_processor/audio_resampler_stream.c

Implementation details:

linear interpolation

uses pos_q16 and step_q16 so phase carries across chunks

supports 16-bit and 32-bit sample containers (your “24-bit” is stored in 32-bit lanes already)

2) PCM stash (simple, linear buffer — not a wrap ring)

This keeps changes minimal and avoids split-buffer logic.

Add to play_manager.c (or put it in a small pcm_stash.* if you prefer):

typedef struct {
    uint8_t *buf;
    size_t cap_frames;     // total frames capacity
    size_t frame_bytes;    // bytes per frame (matches dst format)
    size_t frames;         // frames currently stored, starting at buf[0]
} pcm_stash_t;

static esp_err_t pcm_stash_init(pcm_stash_t *s, size_t cap_frames, size_t frame_bytes);
static void pcm_stash_deinit(pcm_stash_t *s);
static size_t pcm_stash_free_frames(const pcm_stash_t *s);
static esp_err_t pcm_stash_append_frames(pcm_stash_t *s, const uint8_t *frames, size_t nframes);
static void pcm_stash_consume_frames(pcm_stash_t *s, size_t nframes); // memmove remainder down

This does an occasional memmove, but with modest sizes (e.g., 2k frames) and max_iters=4 per fill, it’s totally fine on ESP32.

How play_manager changes (high level)
Extend your play_manager_state_t with:

#include "audio_resampler_stream.h"

typedef struct {
    ...
    uint16_t wav_channels;          // from header, 1 or 2

    // fixed output chunk derived from dst format
    size_t out_frames_per_chunk;

    pcm_stash_t stash;
    audio_resampler_stream_t rs;

    bool eof_seen;
} play_manager_state_t;

New helper signatures inside play_manager.c

// Read enough WAV frames (variable bytes) to ensure stash has min_frames.
// Appends to stash in dst format (bit depth + channel count), still at src_rate.
static esp_err_t ensure_stash_frames(size_t min_frames_needed);

// Produce exactly one output queue block (1 KiB). Pads with zero at EOF.
// Returns ESP_OK on success; ESP_ERR_NO_MEM for backpressure; other for fatal.
static esp_err_t produce_one_output_block(uint8_t *dst_block, size_t *out_bytes);

The key logic: fixed output, variable input
Fixed output per enqueue

Always allocate dst_block and fill:

out_frames = s_pm.out_frames_per_chunk;
out_bytes  = out_frames * s_pm.frame_bytes_dst;  // == 1024

Variable input needed

Before producing, compute:

min_in = audio_resampler_stream_min_in_frames(&s_pm.rs, out_frames);

Then:

ensure_stash_frames(min_in);

ensure_stash_frames() reads from file in variable sized reads, looping until stash has enough frames or EOF.

Each read still uses your existing pooled src_block scratch (1 KiB), but the number of reads per output block varies based on ratio and channel/bit depth.

How to append to stash correctly (channel count!)

You currently do bit depth conversion but no channel conversion. Under the stash contract, you must end up with stash frames matching output channels (usually stereo). Options:

Option 1: enforce WAV must match output channels (fastest)

In play_manager_play_wav() after parsing header:

if ((channels == 1 && s_pm.out_cfg.channels == AUDIO_CHANNEL_STEREO) ||
    (channels == 2 && s_pm.out_cfg.channels == AUDIO_CHANNEL_MONO)) {
    return ESP_ERR_NOT_SUPPORTED;
}

Option 2: support mono WAV to stereo output (recommended)

When appending converted frames to stash:

if WAV is mono and output is stereo: duplicate sample into L and R per frame

That upmix step should happen after bit depth conversion (so you only implement it for 16-bit + 32-bit, not 24-bit packed weirdness).

What to do at EOF (and why it matters)

When remaining_bytes == 0 and stash doesn’t have enough input frames to interpolate:

resampler can still produce some output, but eventually it will need i0+1 and it won’t exist.

Two good choices:

Pad the final output block with zeros and enqueue it (simplest; smooth A2DP)

Enqueue a short final chunk (works if your downstream handles it cleanly)

Given your A2DP callback requests fixed lengths and you already zero-fill, option (1) is the cleanest: always enqueue full 1KiB blocks until you’ve flushed the stash; last block may be partly silence.

Drop-in function behavior (more detailed semantics)
audio_resampler_stream_min_in_frames()

Should account for interpolation:

// Need max input index referenced by last output frame + 2 frames.
needed = ((pos_q16 + (out_frames - 1) * step_q16) >> 16) + 2;

audio_resampler_stream_process()

produces exactly out_frames if enough input is available

otherwise produces as many as possible and returns that count (caller pads rest)

updates pos_q16

computes whole frames consumed:

consumed = pos_q16 >> 16;
pos_q16 &= 0xFFFF;
*in_frames_consumed = consumed;

Caller then drops consumed frames from stash.

Why this is “minimal churn” for your repo

You don’t need to touch audio_queue

You don’t need to modify bt_manager

audio_processor_read() stays unchanged

play_manager_fill() still “tries a few blocks per call”

Your existing “rewind on enqueue fail” logic can remain (though if you never advance file state unless you can commit to stash, you’ll use it less)

Strong recommendation: keep audio_util.c::resample_audio() for non-streaming uses

Beep/synth/etc can continue using the stateless helper. WAV playback should switch to streaming resampler.

Next step (so you can implement quickly)

If you want, I can write you a tight patch plan (file-by-file edits) against your current play_manager.c:

add audio_resampler_stream.[ch]

add stash + state fields

replace process_audio_block() with:

ensure_stash_frames(min_in)

audio_resampler_stream_process(...) into one dst_block

enqueue that block

…and I’ll also point out exactly where to enforce or upmix mono WAVs so you don’t get “half-length / wrong-interleaving” playback.

If you tell me your intended supported set:

sample rates (44.1k? 48k? both?)

mono WAVs allowed or stereo only?

output bit depth (16 vs 32 container)
…I’ll tailor the recommended constants (stash cap, out_frames_per_chunk) to your exact A2DP format.