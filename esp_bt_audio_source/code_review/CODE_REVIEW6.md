Here's my latest code. Github Copilot made changes to it and refactored it. Tell me what's good about it. Tell me what's bad about it. Tell me what needs improvement. 

====

I pulled the esp_bt_audio_source_02031214 tree and focused on the refactor areas that tend to regress: boot/init flow (main/main.c), WAV playback (components/audio_processor/play_manager.c), and the new streaming resampler (components/audio_processor/audio_resampler_stream.*).

Below is what’s good, what’s bad / risky, and what I’d change next.

What’s good
1) The refactor is moving in the right architectural direction

You’ve got real component boundaries now (bt_manager, command_interface, audio_processor, nvs_storage, util_safe), with documentation (ARCH.md, MIGRATION.md, docs/architecture_diagram.md etc.). That’s a meaningful step up from “everything in main.c”.

The boot flow in main.c is more explicit about ordering and failure policy (platform services fail-fast, subsystems degrade). That’s a sane policy for a device that still needs to be diagnosable when one subsystem breaks.

2) Streaming resampler + stash is the correct fix for “ends early” (conceptually)

You now have:

audio_resampler_stream_t with Q16.16 phase carry (pos_q16, step_q16)

pcm_stash_t to support variable input reads while producing fixed output chunks

produce_one_output_block() which tries to always output a consistent chunk size

That’s exactly the right pipeline contract for A2DP-style “pull fixed size” audio.

3) Test and instrumentation posture improved

There are host tests (e.g. test_audio_resampler_stream.c, play manager host tests), and you’re tracking frame-based metrics (s_src_frames_read, s_dst_frames_produced, expected dst frames from ratio).

util_safe_* helpers are a good move (it’s easier to enforce buffer contracts in one place than scattered memcpy).

What’s bad / needs improvement (with concrete issues)
A) Critical memory safety bug: mono→stereo upmix overflows src_block

In play_manager.c::ensure_stash_frames() you do mono→stereo “in place” inside src_block:

src_block is a single AUDIO_CHUNK_BLOCK_BYTES (1 KiB) block.

You read up to 1 KiB of mono PCM (e.g. 512 frames at 16-bit mono).

After bit-depth conversion, conv_bytes can still be ~1024 bytes.

Then you upmix by duplicating samples doubling bytes, which can require 2 KiB, but you still write into the 1 KiB buffer.

Processing backwards avoids overlap corruption, but it does not solve capacity. This can cause:

heap corruption

“stops early”

random audio artifacts

crashes far away from the true cause

Fix (minimum change):

Don’t upmix in-place.

Either:

Limit read size when wav_channels==1 && out_channels==2 so the expanded stereo data fits in src_block (i.e., read at most half-block worth of mono frames), or

Upmix directly into the stash (preferred), or into a second temp block sized for stereo.

If you want the most robust approach: convert+upmix into a separate “converted frame scratch” buffer (or write directly into stash->buf + stash->frames*frame_bytes).

B) Correctness bug at EOF: resampler can report consuming more frames than exist

audio_resampler_stream_process():

pads zeros when i0 >= in_frames (EOF)

still advances pos += step_q16 for every output frame

returns *in_frames_consumed = Q16_INT(pos)

If in_frames is small near EOF, Q16_INT(pos) can easily exceed in_frames, which will make:

pcm_stash_consume_frames(&stash, in_frames_consumed) fail (or consume past end if you ever relax checks)

This can directly manifest as playback stopping early at the end of the file.

Fix options:

Clamp consumption in the resampler:

*in_frames_consumed = MIN(Q16_INT(pos), in_frames);

If you consumed all available input, reset pos to 0 (or keep fractional only if you expect more input later — but at EOF you typically don’t).

Change the API contract: resampler should never claim to consume beyond available input frames.

Alternative: if you want “pad to full output block” behavior, do padding at the caller and keep the resampler from advancing beyond input. (Cleaner semantics, easier to test.)

Right now you’re doing padding in both places conceptually (resampler pads; caller also has padding code, though it never triggers if the resampler always returns full out_frames).

C) Backpressure behavior regressed: queue full drops audio

In play_manager_fill() if enqueue fails, you explicitly accept losing that chunk because rolling back stash + phase is “complex”.

That’s an understandable tradeoff, but it does create failure modes that look like:

“file ended early”

missing segments under stress

incorrect “expected vs produced” ratios

And it’s not that complex to do correctly:

Simple rollback pattern:

Before produce_one_output_block():

snapshot stash.frames (and ideally stash head offset if you move to a ring)

snapshot rs.pos_q16

If enqueue fails:

restore stash count

restore rs.pos_q16

keep the produced dst_block for retry (or discard and regenerate deterministically)

You don’t have to rewind the file if you treat the stash as the “commit buffer”.

D) “Fixed 1024 bytes” comments are misleading / brittle

You have comments like “Always output exactly 1024 bytes (256 frames stereo 16-bit).”

But you compute:

out_frames_per_chunk = AUDIO_CHUNK_BLOCK_BYTES / frame_bytes_dst

out_bytes = out_frames * frame_bytes_dst

So output is always 1024 bytes, but not always “256 frames stereo 16-bit”. If you ever run 32-bit or mono, frames per chunk changes.

This is small, but it causes confusion and wrong assumptions later.

E) PCM stash implementation is correct-but-suboptimal

The stash uses a linear buffer + memmove on consume. It’s fine at low rates and small stash sizes, but it’s a predictable CPU cost.

If you keep this design:

it will still likely work OK on ESP32 for small stash sizes (and your current 2048 frames), but…

If you want this to be “production-solid”:

convert stash to a ring buffer (head, tail, frames) and avoid memmove.

this will also make rollback easier (just restore head/tail counters).

What I would improve next (prioritized)
Priority 0 (must fix)

Fix mono→stereo overflow in ensure_stash_frames().

Fix EOF consumption in audio_resampler_stream_process() (clamp in_frames_consumed to available input).

These two are enough to explain “stopping early” on some files.

Priority 1 (strongly recommended)

Make resampler’s “always returns out_frames” behavior explicit and consistent:

Either the resampler can return partial and the caller pads,

or the resampler always fills, but then it must not over-consume input and your caller’s padding branch should be removed to avoid dead code.

Implement stash + phase rollback on enqueue failure (no file rewind needed).

Priority 2 (cleanup / design)

Replace stash memmove with a ring buffer.

Make channel policy explicit:

supported WAV channel counts

supported output channel counts

whether upmix/downmix is supported (and how)

Add targeted tests that match your suspected bug:

mono 44.1kHz → stereo 48kHz, last chunk behavior

very short WAV (shorter than one output block)

forced backpressure (enqueue fail) to ensure no “early end”

Bottom line

Copilot’s refactor got the architecture right (streaming resampler + stash + fixed output blocks), but there are at least two real correctness/safety bugs in the implementation that can absolutely produce “stops early” symptoms:

mono→stereo in-place upmix overflow

EOF over-consumption reporting in resampler

If you want, I can point to the exact code blocks and propose a tight patch strategy (minimal diffs) for both issues, including a safe “upmix into stash” implementation and a corrected audio_resampler_stream_process() consumption rule.

====

For "A) Critical memory safety bug: mono→stereo upmix overflows src_block", I probably will want to do the most robust approach which would be convert+upmix into a separate “converted frame scratch” buffer (or write directly into stash->buf + stash->frames*frame_bytes). For "B) Correctness bug at EOF: resampler can report consuming more frames than exist", I think that "Clamp consumption in the resampler" is the best solution. For "E) PCM stash implementation is correct-but-suboptimal", I might change it back to using a ring buffer instead of a queue. I thought that since I had multiple producers, I wouldn't be able to use a ring buffer though. Can you go into more detail about that?

====

Absolutely. I’ll break this into three parts:

How to do the robust mono→stereo convert+upmix path (scratch buffer vs direct-to-stash)

How to clamp EOF consumption in the resampler cleanly (and what semantics to adopt)

Ring buffer with multiple producers: what’s actually hard, what’s easy, and three architectures that work

1) Robust mono→stereo convert+upmix without overflowing src_block

You’re right: the robust approach is either:

Option A (best): write converted/upmixed frames directly into the stash (no extra copies)

Option B: use a separate “converted scratch” buffer sized for the converted output, then append to stash

Option A: Direct-to-stash (recommended)

This is the cleanest because it avoids extra scratch and avoids overflow by construction.

Flow in ensure_stash_frames() becomes:

Read raw bytes into src_block (still 1KiB).

Convert (bit depth) + upmix/downmix into a pointer that points into the stash free region.

Advance stash frame count by the number of frames appended.

Concretely:

Determine dst_frame_bytes = frame_bytes(out_cfg)

Determine how many frames you can append now:
max_frames_append = pcm_stash_free_frames(stash)

Determine how many source frames you can read this round without exceeding stash capacity:

if WAV is mono and output stereo, you can still read mono frames and expand to stereo in the write step (no overflow).

Use a helper like:

size_t convert_and_upmix_to_stash(
    const uint8_t *src_bytes, size_t src_bytes_len,
    int wav_channels, audio_bit_depth_t wav_bps,
    int out_channels, audio_bit_depth_t out_bps,
    uint8_t *stash_write_ptr, size_t stash_write_frames_cap,
    size_t *frames_written);

Key points:

stash_write_ptr = stash->buf + stash->frames * dst_frame_bytes

stash_write_frames_cap = free_frames

Your conversion function must stop when it hits stash_write_frames_cap.

This eliminates the “double bytes in-place” hazard entirely.

Option B: Converted scratch buffer

If you don’t want conversion writing into stash memory (some folks prefer to keep stash as purely buffered “already validated” frames), then allocate a scratch buffer sized for stereo converted frames:

conv_scratch = malloc(MAX_CONV_FRAMES * dst_frame_bytes)

Fill it from src_block via convert+upmix

Append to stash

This is robust too, but it adds copy overhead and heap pressure unless you allocate once and reuse.

Recommendation: Option A is simplest and fastest.

2) EOF clamp in resampler: correct semantics

Your chosen fix (“clamp consumption in the resampler”) is good, but it must come with a clear semantic contract:

The contract you want

Resampler returns:

out_frames_written (could be == requested, because you pad)

in_frames_consumed (must be <= available input frames)

Resampler may pad output with zeros when it can’t interpolate (EOF)

Resampler must not advance its internal phase as though it consumed nonexistent input frames (or at least it must not report consuming them).

Minimal patch

At the end of audio_resampler_stream_process():

size_t consumed = st->pos_q16 >> 16;
if (consumed > in_frames_avail) consumed = in_frames_avail;
*in_frames_consumed = consumed;

// Keep only fractional part if there is still input expected; but at EOF you can reset
st->pos_q16 &= 0xFFFF;

However, there’s an important nuance:

If you pad (EOF) and still advance pos_q16 for every produced frame, you’ll build up a huge “virtual consumption,” then clamp it back down. That’s fine if you also reset phase to something consistent (typically 0) once you’ve consumed everything and you know you’re at EOF.

A slightly better EOF-aware behavior:

If you detect “input exhausted” (you hit i0+1 >= in_frames_avail), then:

stop advancing phase as though input exists

pad remaining output

set *in_frames_consumed = in_frames_avail (consume everything)

set pos_q16 = 0 (or keep fractional only if you expect more input later—usually you don’t at EOF)

This avoids weird phase carry after EOF.

3) Ring buffer with multiple producers: what’s actually possible?

This is the big conceptual question. You’re correct that a single ring buffer is easiest for single producer / single consumer (SPSC). Multiple producers complicate things, but it’s not impossible; you just have to pick the right architecture.

There are basically three viable patterns for your case:

Pattern 1 (recommended): Keep multiple producers, but funnel them through a single “audio mixer/task”

Idea: multiple producers don’t write into the ring directly. They send messages (commands + buffers) to one dedicated “audio task,” and that task is the only producer that writes to the ring.

Producers: WAV manager, beep generator, I2S ingress, etc.

They post:

“enqueue these frames”

“start wav playback”

“generate beep pattern X”

“switch source to I2S”

Audio task:

is the only writer to the ring buffer

arbitrates mixing/priority

ensures consistent format (sample rate/channels/bps) before inserting

✅ Pros

Ring buffer stays SPSC (audio task → BT callback consumer)

Very robust and easy to reason about

Central place to implement priority/mixing policy

Easy to add stats, watermarking, underrun telemetry

❌ Cons

Adds one task and a message queue (small latency + complexity)

Producers can’t just “push bytes”; they must message

This is the cleanest design for embedded audio.

Pattern 2: Keep your block pool + descriptor queue (your current model)

This is actually already a “multi-producer queue” design:

producers allocate blocks from a pool

they enqueue descriptors to a queue

consumer pops descriptors and frees blocks

✅ Pros

Multiple producers are naturally supported

No contention over “ring head”

Producers don’t overwrite each other’s data

❌ Cons

More overhead than a ring (alloc/free bookkeeping)

More fragmentation risk if you ever allocate outside the pool

Harder to do precise rollback (but doable)

If you want multiple producers without an audio arbiter task, this is the simplest.
So the question becomes: do you really want to revert to a ring, or do you want to optimize what you have?

Pattern 3: Multi-producer ring buffer (MPSC) — doable but tricky

Idea: multiple producers write into a shared ring using atomic reservation. Each producer “reserves” a contiguous region by atomically incrementing a write index, then writes into that region.

This requires:

atomic operations

careful wrap handling

memory ordering / barriers

usually per-producer commit markers to avoid consumer reading incomplete writes

✅ Pros

Single ring, low overhead

No block allocator

❌ Cons

Hard to implement correctly on embedded without careful testing

Wrap-around makes reservations nontrivial (you may need to reserve “padding” to end-of-buffer)

Consumer must know which ranges are committed

Debugging is painful

I do not recommend this unless you’re very motivated and want to spend time validating it.

So… why did you think multiple producers prevents ring buffers?

Because a ring buffer is naturally one contiguous stream. If two producers push unrelated audio simultaneously, you need:

a policy for ordering/mixing

a way to avoid interleaving samples incorrectly

a mechanism so they don’t stomp on each other’s writes

That is a control problem, not just a data structure problem.

Key insight

If you have multiple audio sources, you usually want one “audio timeline” anyway.
That means you need an arbiter. Once you have an arbiter, the ring becomes easy again.

What I’d recommend given your project constraints

You already have:

WAV playback (file-driven)

beep generator (synth-driven)

possibly I2S ingest

and an A2DP callback consumer

You also already have a control plane (UART commands) where users pick source or request events.

So I’d recommend Pattern 1:

Single audio task owns the ring

Ring buffer is the primary transport from audio task → A2DP callback

WAV/beep/I2S are “sources” controlled by the audio task

Each source can maintain internal state (e.g., WAV file pointer, resampler state)

Your current “block queue” can remain internal to the audio task if needed, but you may not need it once the ring exists.

This gives you:

predictable pacing

less overhead than block queue

trivial “no early stop” behavior (because the audio task can guarantee constant feeding until EOF)

If you still want multiple producers without an arbiter task

Then your block pool + descriptor queue approach is actually the right fit. In that case, I’d suggest:

keep the queue design

optimize it:

make stash a ring (inside WAV source only)

reduce memmove overhead

tighten enqueue failure rollback

But don’t try to make “multiple independent producers write into one ring” unless you also solve “who decides ordering/mixing”.

Next steps (actionable)

If you tell me which of these is your real intent:

“Only one active source at a time” (WAV or beep or I2S)

“Mix beep over WAV sometimes”

“Mix multiple sources concurrently”

…I can recommend the exact structure:

For (1): you can keep a ring buffer without an arbiter task if you ensure only one producer is active (enforced by a mutex and mode state).

For (2)/(3): you almost certainly want a single audio task arbiter.

And if you want, I can sketch the precise FreeRTOS primitive choices (mutex vs queue vs event group) that keep the A2DP callback safe (no blocking, no heavy locks).

====

I want to hold off on converting to a ring buffer for now. Can you take a closer look at the beep manager, I2S manager and the synth manager? I think the beep manager is fine but I'm worried that the I2S manager and the synth manager might have some similar issues with lossing audio chunks or truncation like the play manager had. A quick review of that code would be good.

====

Yep — I took a closer look at the three modules in your esp_bt_audio_source_02031214.zip:

components/audio_processor/beep_manager.c

components/audio_processor/i2s_manager.c

components/audio_processor/synth_manager.c

and (critically) how they interact with components/audio_processor/audio_queue.c

You’re right to be suspicious about I2S (and, to a lesser extent, how “synth” is integrated). The beep path is comparatively safe.

1) Beep manager quick review
What’s good

Backpressure-aware: it retries enqueue for a bounded time and then either succeeds or returns an error.

Pacing / low-watermark wait: after enqueue it yields/waits to let A2DP drain the queue. That reduces the chance of self-inflicted queue-full storms.

Uses audio_chunk_enqueue_bytes_with_id(...) with a rolling tag_id so logs/telemetry can correlate chunks.

What’s potentially bad

It allocates a 1 KiB chunk buffer with heap_caps_malloc() and reuses it. That’s fine, but you’re still:

relying on heap availability,

copying into the queue pool every time anyway.

If enqueue can’t happen within the deadline, the beep can truncate early (by design). That’s probably acceptable for beeps.

Bottom line: beep manager is “fine” for what it is.

2) I2S manager: you have a real truncation bug (not hypothetical)

This is the big one.

The core problem

Your I2S path uses:

audio_chunk_enqueue_bytes(s_mgr.bufs.proc_buf2, res_size, AUDIO_SOURCE_TAG_CAPTURE)

But audio_chunk_enqueue_bytes() only enqueues ONE 1 KiB block and truncates anything larger:

copy_len = min(len, AUDIO_CHUNK_BLOCK_BYTES);
... enqueue chunk with len=copy_len ...
return true;

There is no loop to enqueue the remainder.

Why that matters in your runtime sizing

In audio_processor.c, your I2S buffers are sized using:

AUDIO_WORK_BUFFER_BYTES = AUDIO_BLOCK_SIZE * 8 * AUDIO_RESAMPLE_MAX_RATIO

with your current constants: 128 * 8 * 8 = 8192 bytes

And you pass that straight in:

.raw_buf_bytes = s_runtime_work_bytes;  // typically 8192

So i2s_channel_read() can return up to ~8192 bytes per read, and you resample/convert into a buffer that can also be thousands of bytes.

Net effect: on each I2S read, you enqueue only the first 1024 bytes and silently discard the rest.

That is absolutely “losing chunks / truncation,” and it will manifest as:

severe audio dropouts,

“sped up” or “gapped” audio,

confusing underflow behavior downstream.

Secondary issues in I2S manager

Enqueue failure is ignored by the task loop:

process_frame() returns ESP_ERR_NO_MEM if enqueue fails,

but the task caller uses (void)process_frame(...) and continues.

That means you’ll keep reading/cycling and drop capture audio aggressively under load rather than throttling.

Resampling is stateless (resample_audio()), so if you ever run I2S at a rate different from the A2DP output, you can get drift/rounding artifacts similar to the old WAV path. (Right now your configure_i2s() sets I2S to the configured rate, so resampling is typically a no-op, but the code is structured as if it might resample.)

What I’d fix (minimal changes)

Pick one of these approaches:

Option A (simplest + lowest risk): read/enqueue in 1 KiB chunks

Limit I2S reads to AUDIO_CHUNK_BLOCK_BYTES (1024) so res_size never exceeds 1024.

You already have I2S_MAX_READ_BYTES defined (4 KiB) — but for your queue API, 1 KiB is the magic size.

That can be as simple as changing the read length passed to i2s_channel_read() from raw_buf_bytes to min(raw_buf_bytes, AUDIO_CHUNK_BLOCK_BYTES) and removing the expectation that a single call carries multiple blocks.

Option B (better): implement a multi-block enqueue API

Add something like:

bool audio_chunk_enqueue_bytes_multi(const uint8_t *data, size_t len, audio_source_tag_t tag);

that loops:

alloc block

copy next 1024

enqueue

repeat until done (or fail)

Then I2S manager can enqueue the whole res_size buffer.

For live capture, if enqueue fails mid-way, you can stop immediately (drop tail) — you don’t need “rewind.”

Option C (best architecture, more work): make I2S producer fixed-output like WAV

Same contract you used for WAV:

produce exactly 1 block per iteration

keep “un-enqueued remainder” in a local stash

only advance the stash when enqueue succeeds

This eliminates loss under backpressure.

What I’d fix about backpressure behavior

Regardless of the enqueue strategy:

If enqueue fails, throttle the read loop (short vTaskDelay(1–5ms)), and/or stop capture until queue watermark recovers.

Track counters:

capture_enqueue_fail_count

capture_bytes_dropped

capture_max_queue_used

3) Synth manager quick review

This one is different: it does not enqueue into the audio queue at all. It generates samples directly into the caller’s output buffer (used as a “keepalive / tone” source when nothing else is available).

What’s good

No queue interactions → no “dropped chunk” mechanism like play_manager had.

Fade logic is there; it can clear force_synth_flag under a lock, so the system can exit synth mode cleanly.

Handles 16-bit and 32-bit container formats.

What’s potentially bad / worth tightening

EOF-like “stop early” equivalent: synth can stop as soon as fade-out completes by clearing force_synth_flag. That’s intentional, but:

if multiple tasks touch force_synth_flag, your lock usage must be consistent everywhere else too (same lock object, same critical section discipline).

Minor code hygiene: the fade decrement block is oddly indented; the logic looks fine, but it’s easy to accidentally change semantics during refactors.

Bottom line: synth manager isn’t going to lose audio chunks the way play_manager did, because it’s not chunk-queue-based. The risk is more about flag/lock correctness and “ending because the mode flag is cleared.”

Key takeaway
Beep manager: fine.
Synth manager: not chunk-droppy; mostly about mode/lock semantics.
I2S manager: currently truncates by design of audio_chunk_enqueue_bytes() + your large buffer sizes.

If you want the quickest “make it correct” fix with minimal refactor, do Option A (read 1 KiB at a time) or Option B (multi-block enqueue). If you want it to behave robustly under backpressure (no silent drops), do Option C (stash + fixed-output producer), which mirrors your improved WAV pipeline.

If you tell me which option you prefer (A/B/C), I’ll outline the exact code edits (function names + where to change) so you can implement it quickly without revisiting the whole audio architecture.

====

Should we just use a ring buffer? Should we just make that change now? Would that fix a lot of the problems?

====

A ring buffer would fix some of what you’re seeing—especially the I2S “I enqueue 8 KB but only 1 KB actually gets queued” class of bug—but it will not automatically fix “multiple producers + backpressure + prioritization” unless you also change the ownership model (who writes to the ring and when).

So the right answer is: yes, a ring buffer can be a net win, but only if you pick the right architecture. Otherwise you can easily trade today’s bugs for harder-to-debug race conditions.

Here’s the decision in concrete terms.

What a ring buffer would fix immediately
1) I2S truncation

Right now, audio_chunk_enqueue_bytes() only enqueues one 1 KiB block. A ring buffer allows you to write arbitrary byte counts (up to free space) without “must be <= 1024” constraints. That would eliminate the biggest immediate correctness issue on the I2S path.

2) Less allocator/pool friction

Your current model depends on:

block pool availability

descriptor queue capacity

A ring buffer reduces those failure modes. Backpressure becomes “ring is full,” and you can either:

block briefly (not in the A2DP callback)

drop newest/oldest

throttle capture/playback producers

3) Lower overhead per byte

You remove per-chunk descriptor overhead and per-block bookkeeping. For sustained streaming (I2S capture), this tends to be simpler and cheaper.

What a ring buffer does not automatically fix
A) Multiple producers

A single ring buffer is naturally:

SPSC (single producer, single consumer) → easy and robust

If you have:

WAV playback producer

I2S capture producer

beep producer

…and they all write concurrently, you need:

ordering policy (“who wins?”)

mixing policy (mix? or exclusive?)

concurrency control (so they don’t stomp each other)

If you don’t solve that explicitly, you’ll still have “audio weirdness,” just in a different form.

B) “Stops early” from resampling/EOF issues

The ring buffer doesn’t fix resampler math or EOF semantics. Your streaming resampler + stash work does. Ring helps transport, not DSP correctness.

C) Real-time constraints

Your A2DP callback cannot block. So ring reads must be non-blocking and fast. That’s fine, but it means the producer side must maintain enough headroom.

The two viable ring-buffer architectures
Option 1 (recommended): One writer (audio task) + ring buffer

All audio sources run in one “audio engine” task. That task:

decides what source is active (WAV vs I2S vs synth)

optionally mixes beep over the active stream

writes PCM into the ring buffer at a steady rate

A2DP callback is the only reader from ring

This makes ring buffer usage trivial (SPSC), and it fixes a lot of problems cleanly.

✅ Pros

Simple concurrency

Clean “no lost chunks” story: producer only advances when it actually writes

Easy to implement backpressure policy and priorities

Easy to add “pause,” “stop,” “drain,” “fade out,” etc.

❌ Cons

Requires some refactor: producers become “sources” called by the audio task, not independent enqueue writers.

This is the architecture most audio systems converge to.

Option 2: Multiple writers to ring (not recommended right now)

You can do a multi-producer ring with atomics/reservations, but it’s:

complex

easy to get wrong on embedded

still requires policy/mixing decisions

I’d avoid this unless you’re aiming for a research-grade audio pipeline.

Would making the change now “fix a lot”?

If you adopt Option 1, yes—it will fix a lot:

I2S truncation disappears (because you write exact bytes into ring).

Queue-full block allocator failures disappear (because there is no pool/descriptor).

Backpressure handling gets simpler and consistent.

The “who is allowed to enqueue” becomes explicit.

But if you just “swap the queue for a ring” and keep multiple producers independently writing, you’ll still have:

concurrency complexity

prioritization ambiguity

potential audio interleaving glitches

So it’s not a one-line replacement; it’s a small architecture change.

A pragmatic compromise: fix today’s worst bug without going ring (fast)

If your goal is “stop losing I2S audio now,” you can do this with one small change:

Implement audio_chunk_enqueue_bytes_multi() that loops and enqueues multiple 1 KiB blocks.

This keeps your current architecture intact, and removes the catastrophic truncation. It’s probably a 30–60 line change plus a few call-site updates.

Then you can decide later whether ring is worth it.

My recommendation

Given where you are (already refactoring WAV into a correct streaming pipeline), I’d do this:

Short-term (today): implement multi-block enqueue or read 1KiB at a time in I2S manager.
This eliminates the most severe loss immediately with minimal risk.

Next step (when ready): move to ring buffer with a single audio-engine task (Option 1).
Don’t attempt multi-producer ring.

That sequence avoids destabilizing everything at once.

If you want to switch now anyway (Option 1), here’s the minimal change set

Create audio_ringbuffer.c/h with:

write() (called only by audio task)

read_nonblocking() (called by A2DP callback)

available_to_write, available_to_read

Add one FreeRTOS task: audio_engine_task

calls wav_source_fill(), i2s_source_fill(), beep_overlay_fill() etc.

writes to ring until high watermark reached, then sleeps briefly

Modify audio_processor_read():

read from ring first

if insufficient bytes: zero-fill remainder and count underrun

This would fix the current I2S loss and simplify your queue logic.

Bottom line

Ring buffer is a good direction, but only if you also centralize writes through one audio task.

If you don’t want that refactor yet, multi-block enqueue fixes the biggest problem with far less risk.

If you tell me which you prefer:

A) quick fix (multi-block enqueue), or

B) commit to ring + audio task now,
I’ll lay out the exact module/API sketch and the smallest path to implement it.

====

I think I'm ok with "Option 1 (recommended): One writer (audio task) + ring buffer".

====

Great — Option 1 is the “audio-engine” architecture, and it will simplify your system a lot. Below is a concrete plan that’s minimally disruptive, keeps your existing modules mostly intact, and eliminates the two big classes of bugs you’ve been fighting:

truncation/loss under backpressure (I2S + WAV + beep)

multiple producers fighting over shared buffering

I’ll describe the target design, the ring buffer API, how the audio task interacts with WAV/I2S/beep/synth, and what you actually change file-by-file.

Target architecture (Option 1)
Ownership

Single producer: audio_engine_task is the only writer into the ring buffer.

Single consumer: A2DP data callback (via audio_processor_read) is the only reader.

“Sources”

WAV, I2S capture, beep, synth become “sources” that can:

generate PCM into a scratch buffer when asked (fill() style), or

provide frames from internal state (WAV decoding/resample state, I2S DMA reads, etc.)

But they do not enqueue blocks anywhere. They only produce bytes when the audio engine asks.

Backpressure

Backpressure is simply: ring buffer near full ⇒ engine stops writing and sleeps.
No more block pool OOM, descriptor queue full, partial enqueue, or “rewind file pointer” gymnastics.

Ring buffer API

Keep it simple and SPSC. Two safe designs:

Design A: single contiguous ring with wrap handling (recommended)

Expose these operations:

typedef struct audio_rb audio_rb_t;

esp_err_t audio_rb_init(audio_rb_t **rb, size_t capacity_bytes, bool in_psram);
void      audio_rb_deinit(audio_rb_t *rb);

// Producer side (audio task)
size_t    audio_rb_write(audio_rb_t *rb, const uint8_t *src, size_t len);

// Consumer side (A2DP callback path)
size_t    audio_rb_read(audio_rb_t *rb, uint8_t *dst, size_t len);

// Non-destructive counts
size_t    audio_rb_available_to_read(const audio_rb_t *rb);
size_t    audio_rb_available_to_write(const audio_rb_t *rb);

Implementation notes:

Use head/tail indices and used_bytes count.

Protect with portENTER_CRITICAL for very short sections (or atomics if you prefer).

Never block inside read(); if not enough bytes, return what you have.

Capacity sizing

Start conservative:

capacity = 32 KB if only PCM @ 48k stereo 16-bit (192 KB/s) → 32 KB ≈ 167 ms

If you have PSRAM, 128 KB gives you more slack and reduces underruns.

The audio engine task
High-level loop

Maintain a target watermark:

LOW_WATERMARK (e.g., 8 KB): if ring drops below this, refill aggressively

HIGH_WATERMARK (e.g., 24 KB): stop refilling

Pseudo:

for (;;) {
  size_t free = audio_rb_available_to_write(rb);
  size_t used = audio_rb_available_to_read(rb);

  if (used < LOW_WATERMARK) {
      while (audio_rb_available_to_write(rb) >= CHUNK_BYTES && audio_rb_available_to_read(rb) < HIGH_WATERMARK) {
          // produce exactly CHUNK_BYTES into scratch
          produce_pcm(scratch, CHUNK_BYTES);
          audio_rb_write(rb, scratch, CHUNK_BYTES);
      }
  }

  vTaskDelay(pdMS_TO_TICKS(2));
}

CHUNK_BYTES

Pick something like 1024 or 2048 bytes to match your existing “block” sizing and to keep resampler fill predictable.

Source selection & mixing policy

You need to decide whether you want:

exclusive source selection (WAV or I2S or synth)

plus optional beep overlay (mix beep on top of current stream)

This maps cleanly to the engine:

Decide “base stream”:

if WAV active → base = WAV

else if I2S active → base = I2S

else if force synth → base = synth

else → silence

If beep active:

generate beep frames (same format)

mix into base (clamped add with scaling)

This eliminates “multiple producers” entirely.

How each module changes
1) audio_processor_read()

Becomes:

read from ring into buffer

if short, zero-fill remainder

update underrun stats

No queue draining logic needed; residual buffering becomes much simpler (you can drop the residual concept if ring always stores PCM in fixed format, because read() can provide exactly what’s available).

2) WAV / play_manager

Instead of enqueuing to audio_queue, it becomes:

wav_source_fill(uint8_t *dst, size_t dst_bytes, size_t *bytes_written)

produces PCM in output format (stereo/mono, bit depth) and output sample rate

pads zeros at EOF

when EOF reached and stash drained → marks inactive

Your streaming resampler + stash code is already close to this; it just needs to target a caller-provided buffer instead of “enqueue blocks”.

3) I2S manager

Instead of reading huge buffers and enqueuing, it becomes:

i2s_source_fill(uint8_t *dst, size_t dst_bytes, size_t *bytes_written)

read from I2S into a temp buffer (or directly into dst if format matches and alignment is ok)

perform format conversion if needed

if not enough I2S data immediately, return fewer bytes (engine will retry next loop)

Critically: no truncation. You fill exactly what was read. Ring stores it.

4) Synth manager

Already produces directly into an output buffer. That’s perfect.

Just change the call site to:

synth_fill(dst, frames) used as base source when active.

5) Beep manager

You have two good options:

Option A (recommended): make beep a “source” like synth:

It has internal state (phase, remaining bytes)

beep_fill(dst, dst_bytes) writes beep PCM directly

Engine mixes beep over base stream

Option B: keep existing beep manager as a command-driven “start beep” state machine, but remove all queue enqueueing, and instead store beep state in the manager and expose beep_fill().

File-by-file change plan (minimal disruption)
New files

components/audio_processor/audio_ringbuffer.h

components/audio_processor/audio_ringbuffer.c

optionally tests

Modify

components/audio_processor/audio_processor.c

initialize ring buffer in audio_processor_init()

start audio_engine_task in audio_processor_start()

components/audio_processor/audio_processor_read.c

replace queue consumption with ring read

components/audio_processor/play_manager.c

replace enqueue logic with wav_source_fill(dst, bytes) logic

keep resampler + stash

components/audio_processor/i2s_manager.c

replace enqueue with i2s_source_fill(dst, bytes) logic

keep configure/start/stop

components/audio_processor/beep_manager.c and synth_manager.c

ensure they expose fill APIs that write into buffer

remove any queue enqueue calls

components/bt_manager/bt_streaming_manager.c

no structural changes; it still calls audio_processor_read()

Delete or deprecate

audio_queue.c can be kept temporarily but should become unused in the “engine path”.

Eventually remove to reduce complexity.

What this fixes right away

The I2S truncation bug disappears completely.

WAV playback won’t drop blocks because enqueue failed; it simply won’t “commit” bytes to the ring unless there’s space (and the engine can manage that).

Beep/synth/WAV mixing policy becomes explicit rather than “who enqueued last”.

Your stats become simpler and more truthful: requested bytes vs read bytes vs underrun bytes.

Implementation details that matter
A2DP callback constraints

Must not block.

Must not allocate.

Must do minimal locking.

So the ring buffer’s read() should either:

use a very short critical section to copy out bytes, or

use lock-free atomics.

A short critical section is fine if you copy at most len (usually ~ a few hundred bytes) and don’t do heavy work inside the lock.

Memory placement

Prefer DRAM for ring if you care about latency.

PSRAM is fine for larger buffering, but you may want to keep a smaller DRAM ring for real-time.

Audio format

Pick a single internal PCM format for the ring:

stereo, signed 16-bit little-endian, at the configured BT sample rate
(or 32-bit container if that’s what your A2DP expects)

The engine is responsible for converting everything to that format before writing.

The one question you should decide now (no need to answer with a long back-and-forth)

Do you want:

exclusive sources (WAV or I2S or synth) + beep overlay
or

true mixing between I2S + WAV etc.

Most systems pick (1), and it matches your current design assumptions.

If you want, I can go from this plan to a concrete “migration checklist” with exact function signatures you should introduce (e.g., wav_source_fill, i2s_source_fill, beep_fill) and how the engine decides which one to call, aligned to your current config types (audio_config_t, tags, etc.).

====

How will the ring buffer detail with the meta data for each audio chunk?

====

A ring buffer doesn’t naturally carry “per-chunk metadata” the way your current (block + descriptor) queue does. With a single continuous byte stream, you have to decide whether you still need metadata at playback time, and if so, how to encode it.

In Option 1 (one writer audio task), you have three good approaches. Which one is best depends on what you use metadata for today (logging/correlation, priority/mixing, debugging, volume, timestamps).

0) First principle: most “metadata” is only needed at write time, not read time

Once you move to a single audio engine task, it already knows:

which source it’s emitting now (WAV vs I2S vs synth vs silence)

whether beep overlay was applied

what sample rate/format it’s writing (fixed)

So you can keep a lot of the metadata as counters and events, not per-byte tags.

If metadata is only for diagnostics (“how many bytes from WAV vs I2S?” “underruns?”), you don’t need to store it in the ring at all.

Approach A (recommended): Keep metadata as side-band events + counters
What you do

Ring buffer stores only PCM bytes.

Audio engine maintains stats:

bytes_written_by_source[WAV/I2S/SYNTH/SILENCE]

underrun_bytes (consumer side)

drops (if you choose to drop)

beep_overlay_frames

When something changes, emit an event:

“WAV_START file=X”

“WAV_END”

“I2S_START/STOP”

“BEEP_START/END”

“FORMAT_CHANGED” (rare; ideally never)

Why this works well

Zero runtime overhead in A2DP callback (no tag parsing).

Metadata is still available and more meaningful: you know when sources changed, and you can report totals.

When this is enough

Your metadata is currently used for logging / debugging / CLI “status”

You don’t require the consumer to behave differently per chunk

This is the design most real-time audio pipelines use.

Approach B: Put “frame markers” in a small parallel metadata ring
What you do

Keep two rings:

PCM ring: raw PCM bytes

Metadata ring: entries describing contiguous spans in the PCM ring

Example metadata entry:

typedef struct {
  uint32_t start_byte_offset;   // relative to PCM ring write index mod capacity
  uint32_t byte_len;
  uint8_t  source;              // WAV/I2S/SYNTH/SILENCE
  uint32_t id;                  // optional: tag_id / sequence
} audio_span_meta_t;

The audio engine, whenever it writes N bytes of PCM from a given “mode”, also pushes one metadata entry describing that span.

Consumer reads PCM normally. If you want, you can also expose a debugging function that reads metadata spans to say “the last 2 seconds were mostly WAV with 3 beep overlays,” etc.

Pros

Preserves a lot of what your current chunk tags gave you.

Still cheap because metadata is per span, not per frame.

Cons

You need to keep the metadata ring consistent with the PCM ring:

if PCM overwrites old data (because ring is full and you drop old), metadata must drop corresponding entries too

if you never overwrite (you just block/throttle), it’s simpler.

When to use

You truly want “timeline reconstruction” for debugging.

You want accurate accounting of mixed overlays (e.g., beep applied over WAV).

Approach C: Embed metadata in-band (generally not recommended)

You’d “interleave” a small header every X bytes, like:

[META][PCM][META][PCM]...

This is usually a bad fit because:

A2DP consumer wants a pure PCM stream.

You’d have to strip metadata at read time (extra CPU/complexity).

Any bug corrupts audio.

I’d avoid this.

How beep overlay affects metadata

Beep overlay is mixing, so “source tags per chunk” become ambiguous:

Is the chunk WAV or BEEP?

It’s both.

With Option 1, the right model is:

Base source = WAV/I2S/SYNTH/SILENCE

Overlay flags = beep_on (or multiple overlays later)

So your metadata, if you keep it, should be a bitmask, not a single source enum:

enum {
  AUDIO_FLAG_BASE_WAV   = 1<<0,
  AUDIO_FLAG_BASE_I2S   = 1<<1,
  AUDIO_FLAG_BASE_SYNTH = 1<<2,
  AUDIO_FLAG_BEEP_OVER  = 1<<3,
};

Or split:

base_source + overlay_flags

What I recommend for you (practical)

Given your current goals (fix truncation/loss and keep system debuggable):

Start with Approach A

Implement strong counters + state events in the audio engine.

Add a cmd status report that shows:

current base source

beep overlay active?

bytes written per source (since boot + since last reset)

ring fill level

consumer underrun count

That gets you 90% of the value with 10% of the complexity.

Only add Approach B if you miss per-chunk correlation

If you later find you need “per-span timeline” for debugging, add the parallel metadata ring. It’s compatible with A, and you can add it incrementally.

Concrete API sketch for Approach A (what you’d implement)

In audio_engine:

typedef struct {
  uint64_t bytes_by_source[NUM_SOURCES];
  uint64_t beep_overlay_frames;
  uint64_t rb_underrun_bytes;
  uint32_t source_switch_count;
  uint32_t last_source_change_ms;
  uint8_t  current_base_source;
  bool     beep_active;
} audio_engine_stats_t;

void audio_engine_get_stats(audio_engine_stats_t *out);

Update stats on each write into ring:

increment bytes_by_source[base] += bytes_written

if beep mixed: beep_overlay_frames += frames_in_chunk

if source change: increment source_switch_count

In audio_processor_read() (consumer side), update:

rb_underrun_bytes += (requested - actual_read) if short

Bottom line

A ring buffer handles audio as a continuous stream, so you shouldn’t try to preserve “metadata per chunk” the same way. With the one-writer audio engine, most metadata becomes engine state + counters, which is cleaner and often more useful.

If you tell me what you currently use AUDIO_SOURCE_TAG_* and tag_id for (logging only? mixing decisions? debugging timeline?), I can tell you definitively whether Approach A is sufficient or if you should implement the parallel metadata ring (Approach B).

====

The main purpose of the audio metadata was to debug the ring buffer or the queue. With that I could easily see if audio chunks were stuck or if they weren't getting cleared out.

====

Got it. If metadata is mainly for debugging the transport (queue/ring) — “are chunks stuck?”, “are they being drained?”, “what source wrote them?”, “what got dropped?” — then you don’t need per-sample or in-band metadata. You want observability of buffer dynamics.

With the one-writer ring buffer architecture, the best equivalent is:

Side-band events + counters (cheap, always on)

Optional: a debug “span log” ring (small, bounded, very useful) that records each write into the PCM ring as a “span” with an ID, source, and ring offsets — basically chunk metadata without chunking audio itself.

This gives you the same “stuck chunk” debugging power, but in ring-buffer terms: “stuck span” = “read pointer isn’t advancing” or “producer is repeatedly writing but consumer isn’t draining.”

What “stuck chunk” means in ring-buffer land

Queue model:

stuck chunk = descriptor still in queue, not popped, or block not released

Ring model:

stuck = read pointer not advancing (consumer not reading / callback not invoked / blocked)

or write pointer not advancing (producer stalled because ring is full or engine stopped)

or both pointers move but used_bytes stays flat (producer and consumer matched; not actually stuck)

So you want to observe:

write_idx, read_idx, used_bytes

write events (who wrote, how much, what ID)

read events (how much was consumed, underrun)

Recommendation: implement a “span log” debug ring
Idea

Every time the audio engine writes PCM into the audio ring, you also append a metadata record:

typedef struct {
  uint32_t seq;          // monotonically increasing
  uint32_t t_ms;         // timestamp
  uint32_t write_pos;    // ring offset BEFORE write (mod capacity)
  uint32_t bytes;        // bytes written
  uint32_t used_after;   // used bytes after write
  uint8_t  base_source;  // WAV/I2S/SYNTH/SILENCE
  uint8_t  flags;        // e.g., BEEP_OVERLAY
  uint16_t underrun_hint;// optional: consumer underrun since last
} audio_rb_span_t;

Store these in a small fixed-size debug ring (e.g., 256 entries). This is independent from the PCM ring; it’s just log entries.

Why this solves your debugging use case

You can dump the last N spans and instantly see:

“Engine wrote 1024 bytes every 10ms for 2 seconds, then stopped”

“Ring used_bytes hit capacity; writes stopped” (backpressure)

“Consumer reads were tiny / underruns exploding”

“We switched from WAV to I2S here”

You can detect “stuck” by seeing spans continue but reads not happening, or vice versa.

Keep it behind a debug flag

Gate it behind CONFIG_AUDIO_DEBUG_SPANS or a runtime boolean.

Also add cheap always-on counters

These are simpler than span logs and useful even in production builds:

rb_used_bytes_peak

rb_underrun_bytes_total

rb_overrun_events (if you ever decide to overwrite/drop)

engine_write_calls, engine_write_bytes_total

consumer_read_calls, consumer_read_bytes_total

Plus per-source totals, which help sanity-check mixing/state.

How to implement “is something stuck?” detection

You can implement a small watchdog check in the audio engine task every second:

if used_bytes hasn’t changed for X ms and:

WAV is active (should be producing) AND used_bytes low → consumer might not be requesting (A2DP stopped)

WAV active AND used_bytes high near full → producer stuck because ring full (consumer not draining)

if used_bytes continuously near zero and underruns increasing → producer not keeping up

Emit a concise diagnostic line:

DIAG|AUDIO_RB|STALL|mode=WAV|used=2048|free=30720|underruns=123|last_write_ms=...|last_read_ms=...

This replaces “chunk stuck” debugging with “flow stuck” debugging, which is the correct abstraction for a ring.

What to expose in your UART STATUS command

Add a AUDIO STATUS report like:

ring: capacity / used / free / peak used

writer: last write time, bytes written, current base source + flags

reader: last read time, bytes read, underrun bytes, underrun events

(optional) last 10 span records

This gets you immediate visibility during bring-up.

Practical: how this maps to your old chunk tags

Old:

source_tag, tag_id, enqueue timestamp, chunk len, queue depth

New:

base_source, span.seq, t_ms, write_pos, bytes, used_after

You’ll find the new model often gives better insight, because it’s directly about ring state, not inferred from queue depth.

TL;DR

For “debug transport health,” do this:

Always-on: counters + ring fill metrics (used/free/peak) + last read/write times

Debug build: span log ring (256 entries) that records every engine write (who/what/how much/where)

That gives you the same value your chunk metadata did, without complicating the PCM stream or the A2DP callback.

If you want, I can propose exact structs and a minimal command output format consistent with your existing DIAG|... conventions, so you can grep it and graph it.

====

Yeah. This is what I had originally - two ring buffers. One ring buffer was for the audio data and the other ring buffer had the meta data for the corresponding audio data in the audio ring buffer. There was an issue with the data in the audio ring buffer getting cleared out but not the metadata ring buffer so they end up being out of sync. That prompted to go to a queue with a data structure with the metadata and the audio data together.

====

That failure mode makes complete sense: two independent rings drift out of sync whenever one side advances without the other (or wrap/overwrite happens asymmetrically). The “data+metadata coupled” queue eliminated that class of bugs by construction.

If you go back to audio ring + metadata ring, you can make it robust—but you have to change the coupling rule. The key is:

Never advance the audio ring independently of the metadata ring.
Metadata isn’t “corresponding” by position; it must be a commit log that is written/consumed in lockstep with audio.

Since you’re adopting Option 1 (single writer audio task), you’re in a much better place: you can enforce coupling centrally and make desync basically impossible.

Below are three designs that avoid the out-of-sync problem, ordered from simplest to strongest.

Design 1 (recommended): “Span log” metadata ring that is not position-coupled

Instead of trying to keep metadata parallel to audio bytes, treat metadata as a write-ahead log of writes.

How it works

Audio ring stores PCM bytes.

Metadata ring stores span entries describing each committed write to the audio ring:

starting offset (in audio ring)

length

sequence number

source flags

ring used_after

Consumer does not need metadata to read PCM. It reads PCM as usual.

Metadata is for diagnostics and is read via a debug command (not in real-time path).

Why it cannot desync like before

Because metadata isn’t “expected to match audio position 1:1.” It’s an append-only history. Even if older audio is overwritten (or drained), metadata still describes past writes; you can detect overwrite by comparing sequence or offsets.

What you gain

Debugging: you can see the last 256 writes and how the ring moved.

No requirement that metadata be cleared when audio is cleared.

You never again have “audio empty but metadata still present” as a correctness issue; it just means “no recent writes” or “consumer drained.”

This design is the safest and lowest-risk.

Design 2: Commit-coupled dual rings (lockstep reservation/commit)

If you want metadata to remain “corresponding” to audio in a strict way (like your original), you must implement atomic commit across both rings.

Rule

A “write” is a transaction:

Reserve space in audio ring for N bytes (or fail)

Reserve 1 metadata entry in metadata ring (or fail)

Copy audio into ring

Write metadata entry

Commit both together (make them visible)

And similarly, a “read/drain” is a transaction:

Determine how many bytes to consume from the head of audio ring

Consume metadata entries whose spans are fully consumed

Commit the consumption

Why you desynced before

Typically because:

audio ring got advanced/drained by consumer,

metadata ring didn’t get drained accordingly, or

overwrite policies differed.

How to prevent it

Because you now have a single consumer, you can implement a “drain metadata spans based on consumed bytes” function that runs whenever the consumer advances.

But this is more complexity than you need if metadata is only for debug.

Design 3: Keep metadata inside the audio ring as a separate stream (header stream)

You keep “two streams” but inside one ring by storing a header+payload framing. This is basically reinventing your queue, and it makes the consumer path heavier. I don’t recommend it given your constraints.

What I recommend for your situation

Because you said:

“The main purpose … was to debug the ring buffer or the queue.”

That means you do not need metadata to be consumed in real-time or to reconstruct a perfect correspondence. You need visibility into the behavior.

So you should use Design 1: span log.

It gives you better debugging than your original dual-ring coupling, without the sync hazards.

Concrete “span log” implementation details
Metadata entry

typedef struct {
  uint32_t seq;
  uint32_t t_ms;

  uint32_t write_off;     // audio ring offset (mod capacity) BEFORE write
  uint32_t bytes;         // bytes written
  uint32_t used_after;    // audio rb used bytes after write

  uint8_t  base_source;   // WAV/I2S/SYNTH/SILENCE
  uint8_t  flags;         // BEEP_OVERLAY, etc.
  uint16_t reserved;
} audio_rb_span_t;

Producer (audio engine) rule

When you write to audio ring, you also push a span log entry after the write succeeds:

size_t wrote = audio_rb_write(rb, buf, len);
if (wrote == len) {
   span_log_push(seq++, now_ms(), off_before, len, used_after, src, flags);
}

If partial write occurs (ideally you avoid this by writing only when free >= len), you can log a special span with bytes=wrote and a flag PARTIAL_WRITE.

Consumer rule

Consumer doesn’t touch span log at all.

Debug command

AUDIO RB DUMP prints:

current read_off, write_off, used, free, peak

last 20 span entries (seq, time delta, bytes, used_after, source)

This alone will tell you whether the ring is “stuck”, “draining”, “oscillating”, “overrunning”, etc.

Why the old “audio drained but metadata not drained” isn’t a problem anymore

In the old design, you were treating metadata as a required mirror of audio occupancy (“metadata ring must empty when audio empties”). That’s brittle.

In the span log design:

metadata can remain even if audio drains because it’s historical

you can still see that consumer drained because used_after trends down and no new writes happen, or writes happen but used stays near 0 (consumer keeping up)

So you don’t need strict sync.

If you do want strict sync anyway (for assertions)

You can add an invariant check in debug builds:

Maintain a counter in audio ring: total_bytes_committed (monotonic 64-bit)

Maintain in consumer: total_bytes_consumed (monotonic)

Each span entry includes total_bytes_committed_after (or seq implies it)

You can assert:

total_bytes_consumed <= total_bytes_committed

used_bytes == total_bytes_committed - total_bytes_consumed modulo capacity (if you don’t overwrite)

This gives you the same “are we out of sync?” detection but at a higher level.

Bottom line

Your earlier two-ring approach failed because it tried to maintain positional correspondence between independent buffers. Don’t do that again.

Use:

audio ring for PCM

metadata span log ring as a write history (not occupancy mirror)

It’s robust, easy to implement, and gives you exactly the transport debugging visibility you wanted—without the desync trap that pushed you to the queue design.

====

Ok. I think I'm ok with "Design 1 (recommended): “Span log” metadata ring that is not position-coupled".

====

Great — Design 1 is the right fit for your stated goal (“debug whether audio is stuck/draining”) without recreating the sync hazards that burned you before.

Below is a concrete, implementable spec: data structures, APIs, concurrency rules, and the debug commands/log output that will give you the same observability you had with per-chunk tags, but safely.

1) What the “span log” is (and is not)
It is

A small ring of write events emitted by the single producer (audio engine task) whenever it successfully commits PCM bytes into the audio ring buffer.

It is not

A structure that needs to be “drained” in sync with audio consumption. It’s intentionally a history.

2) Span entry schema

Keep it small and fixed-size so you can store it in IRAM/DRAM and dump quickly.

typedef enum {
  AUDIO_BASE_SILENCE = 0,
  AUDIO_BASE_WAV     = 1,
  AUDIO_BASE_I2S     = 2,
  AUDIO_BASE_SYNTH   = 3,
} audio_base_source_t;

enum {
  AUDIO_SPAN_FLAG_BEEP_OVERLAY   = 1 << 0,
  AUDIO_SPAN_FLAG_PARTIAL_WRITE  = 1 << 1,
  AUDIO_SPAN_FLAG_UNDERRUN_SINCE = 1 << 2,  // optional diagnostic
};

typedef struct {
  uint32_t seq;          // monotonically increasing
  uint32_t t_ms;         // esp_log_timestamp() snapshot

  uint32_t write_off;    // audio RB write offset BEFORE write (mod capacity)
  uint32_t bytes;        // bytes written this event
  uint32_t used_after;   // rb used bytes AFTER write

  uint8_t  base_source;  // audio_base_source_t
  uint8_t  flags;        // overlay + partial + other bits
  uint16_t reserved;

  uint32_t underrun_bytes_total; // snapshot (optional but very useful)
} audio_span_t;

Why include used_after and underrun_bytes_total?
Because you can tell immediately if:

producer is writing but used_after stays low → consumer keeping up (fine)

producer is writing and used_after climbs to near full → consumer not draining (stuck)

underruns are rising → producer not keeping up or consumer requests too aggressively

3) Span log ring buffer implementation

This is independent from your PCM ring. It’s a small fixed ring of audio_span_t.

typedef struct {
  audio_span_t *buf;
  uint32_t cap;       // number of entries
  uint32_t head;      // next write index
  uint32_t count;     // number of valid entries (<= cap)
  uint32_t seq;       // next sequence number
} audio_spanlog_t;

API

esp_err_t audio_spanlog_init(audio_spanlog_t *sl, uint32_t cap, bool in_psram);
void      audio_spanlog_deinit(audio_spanlog_t *sl);

void audio_spanlog_push(audio_spanlog_t *sl,
                        uint32_t t_ms,
                        uint32_t write_off,
                        uint32_t bytes,
                        uint32_t used_after,
                        audio_base_source_t base,
                        uint8_t flags,
                        uint32_t underrun_total);

uint32_t audio_spanlog_snapshot(const audio_spanlog_t *sl,
                                audio_span_t *out,
                                uint32_t max_out);

Concurrency rules (very important)

Only the audio engine task calls push() (single writer).

Debug command handler calls snapshot() (reader).

Use a short critical section around writes and snapshot.

Because this is not in the A2DP callback path, you can use a mutex too, but critical section is simplest.

This cannot “desync” with audio because it doesn’t attempt to be a mirror of occupancy.

4) When to emit a span log entry

Emit exactly once per successful commit to the PCM ring:

Before write: capture write_off_before = rb->write_idx

Perform write (ideally only if free >= len so it’s never partial)

After write: compute used_after

Push span: (write_off_before, len, used_after, base, flags, underrun_total)

Partial write policy

Prefer to avoid partial writes entirely by checking free space first. But if you allow partial writes:

set AUDIO_SPAN_FLAG_PARTIAL_WRITE

record actual bytes written

For debugging, partial writes are worth logging because they correlate strongly with underruns.

5) What to log on reads / underruns

You don’t need to log spans on reads. Keep the consumer path light.

But you should maintain consumer counters:

consumer_total_read_bytes

consumer_underrun_bytes_total

last_read_ms

Then, on the producer side, periodically snapshot consumer_underrun_bytes_total into the spans. That gives you correlation without adding overhead to the callback.

6) Debug command output: what you should show

I’d add a CLI command like:

AUDIO RB STATUS

AUDIO RB DUMP [N]

STATUS should show

PCM ring: capacity / used / free / peak used

writer: last write ms, current base source, beep overlay active

reader: last read ms, total underrun bytes, underrun events

span log: cap, count, latest seq

DUMP should show last N spans (e.g., 20)

Example line format that’s grep-friendly:

DIAG|SPAN|seq=10423|t=123456ms|off=8192|bytes=1024|used=24576|base=WAV|flags=BEEP|underrun=0

If you print used each time, it’s immediately obvious if the ring is filling or draining.

7) Why this solves your original “out of sync” issue

Your previous dual-ring approach treated metadata occupancy as if it must “match” audio occupancy. That’s where you got desync.

Here:

metadata is not occupancy

it’s a historical record of producer behavior

So:

audio can drain completely while metadata still exists → that’s fine, and useful

if audio overwrites old data (if you ever allow overwrite), metadata still describes what happened

No coupling → no drift failure.

8) Minimal integration points in your Option 1 architecture

Where to put it:

The span log should live with the PCM ring, likely inside audio_processor state:

audio_rb_t rb;

audio_spanlog_t spans;

Where to call push():

In the audio engine task, immediately after each successful audio_rb_write().

Where to read it:

In a command handler (cmd_audio_rb_dump) that calls audio_spanlog_snapshot().

9) Guardrails / invariants to catch real bugs early

In debug builds, add assertions:

used_after <= capacity

bytes <= CHUNK_BYTES (if you expect fixed-size writes)

seq monotonic

if you never allow overwrite: audio_rb_write() should only be called when free >= bytes

And expose “stall detectors”:

if used is near full and last_read_ms is old → consumer stuck

if used is near empty and underruns increasing → producer not writing fast enough or source ended

Next step

If you want, I can translate this into a very concrete patch list (files + functions) aligned to your current names:

audio_ringbuffer.[ch] (PCM)

audio_spanlog.[ch] (metadata)

audio_engine_task.c writes + span push

command_interface adds AUDIO RB STATUS/DUMP

Even without code patches here, the above spec is enough to implement cleanly and avoid the desync pitfall you had before.

