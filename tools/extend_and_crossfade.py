#!/usr/bin/env python3
"""
Repeat/concatenate a short raw PCM .bin capture to reach a target duration
but apply a small linear crossfade between repeats to hide loop boundaries.

Assumes 16-bit signed little-endian samples by default (sampwidth=2); supports
configurable sample rate, channels, and sample width.

Usage:
  python3 tools/extend_and_crossfade.py \
        --in esp_bt_audio_source/test/test_app/build/worker_long.bin \
        --out esp_bt_audio_source/test/test_app/build/worker_long_1s_xfade.wav \
    --seconds 1.0 --crossfade-ms 6

The script trims trailing partial frames, repeats the chunk as needed,
and applies a per-frame linear crossfade of `crossfade_ms` milliseconds
between adjacent chunks.
"""
import argparse
import os
import math
import wave
from array import array


def clamp_i16(x):
    if x > 32767:
        return 32767
    if x < -32768:
        return -32768
    return x


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--in', dest='infile', required=True, help='Input .bin file (raw PCM 16-bit LE)')
    p.add_argument('--out', dest='outfile', required=True, help='Output .wav path')
    p.add_argument('--seconds', dest='seconds', type=float, default=1.0, help='Target duration (s)')
    p.add_argument('--rate', dest='rate', type=int, default=44100, help='Sample rate (Hz)')
    p.add_argument('--channels', dest='channels', type=int, default=2, help='Channels (1=mono,2=stereo)')
    p.add_argument('--sampwidth', dest='sampwidth', type=int, default=2, help='Bytes per sample (2=16-bit)')
    p.add_argument('--crossfade-ms', dest='crossfade_ms', type=float, default=6.0, help='Crossfade length in ms between repeats')

    args = p.parse_args()

    infile = args.infile
    outfile = args.outfile
    rate = args.rate
    channels = args.channels
    sampwidth = args.sampwidth
    target_seconds = args.seconds
    crossfade_ms = args.crossfade_ms

    if sampwidth != 2:
        raise SystemExit('Only 16-bit (sampwidth=2) is supported in this simple tool')

    if not os.path.isfile(infile):
        raise SystemExit(f'Input file not found: {infile}')

    with open(infile, 'rb') as f:
        data = f.read()

    frame_bytes = channels * sampwidth
    if len(data) == 0:
        raise SystemExit('Input file is empty')

    full_frames = len(data) // frame_bytes
    if full_frames == 0:
        raise SystemExit('Input file does not contain a full audio frame')

    # Trim to whole frames
    data = data[: full_frames * frame_bytes]

    # Convert to array('h') of int16 samples (interleaved channels)
    samples = array('h')
    samples.frombytes(data)

    frames_per_chunk = full_frames
    target_frames = int(math.ceil(target_seconds * rate))
    # Effective additional frames contributed by each repeat after the first
    # is (frames_per_chunk - crossfade_frames). Compute repeats to reach
    # at least target_frames.
    if frames_per_chunk == 0:
        repeats = 1
    else:
        # We'll compute repeats after establishing crossfade_frames below,
        # so initialize repeats to 1 for now and recalc later.
        repeats = 1

    crossfade_frames = int((float(rate) * float(crossfade_ms)) / 1000.0)
    if crossfade_frames < 1:
        crossfade_frames = 1
    # Ensure the crossfade is not longer than half the chunk; otherwise
    # each repeat will only add a tiny remainder and the output will be
    # much shorter than requested. Cap to half the chunk length.
    max_crossfade = max(1, frames_per_chunk // 2)
    if crossfade_frames > max_crossfade:
        crossfade_frames = max_crossfade

    # Recalculate repeats based on effective frames added per repeat
    if target_frames <= frames_per_chunk:
        repeats = 1
    else:
        per_repeat_add = frames_per_chunk - crossfade_frames
        if per_repeat_add <= 0:
            per_repeat_add = 1
        additional_needed = target_frames - frames_per_chunk
        repeats = 1 + int(math.ceil(float(additional_needed) / float(per_repeat_add)))

    # Prepare output sample array
    out = array('h')

    chunk_len_samples = frames_per_chunk * channels
    crossfade_len_samples = crossfade_frames * channels

    # helper to append chunk whole or partially
    def append_chunk(chunk_samples):
        out.extend(chunk_samples)

    # source chunk as array slice view
    chunk = samples  # array of int16 length = frames_per_chunk * channels

    for r in range(repeats):
        if r == 0:
            # initial chunk: append whole
            append_chunk(chunk)
        else:
            # perform crossfade: mix last crossfade_frames of 'out' with first crossfade_frames of chunk
            if crossfade_len_samples > 0:
                out_tail_start = len(out) - crossfade_len_samples
                # mix per-sample
                # Use a raised-cosine (half-cosine) window for smoother
                # crossfades which reduces spectral artifacts vs linear fades.
                # window_in(t) = 0.5 * (1 - cos(pi * t)) where t in [0,1]
                denom = float(crossfade_frames - 1) if crossfade_frames > 1 else 1.0
                for k in range(crossfade_frames):
                    t = float(k) / denom
                    win = 0.5 * (1.0 - math.cos(math.pi * t))
                    wout = 1.0 - win
                    for ch in range(channels):
                        out_idx = out_tail_start + k * channels + ch
                        chunk_idx = k * channels + ch
                        mixed = int(round(out[out_idx] * wout + chunk[chunk_idx] * win))
                        out[out_idx] = clamp_i16(mixed)
                # append remainder of chunk after the crossfade region
                append_chunk(chunk[crossfade_len_samples:])
            else:
                append_chunk(chunk)

    total_frames = len(out) // channels

    # Write WAV
    os.makedirs(os.path.dirname(outfile), exist_ok=True)
    with wave.open(outfile, 'wb') as w:
        w.setnchannels(channels)
        w.setsampwidth(sampwidth)
        w.setframerate(rate)
        w.writeframes(out.tobytes())

    print(f'Wrote {outfile}: {total_frames} frames, repeats={repeats}, crossfade_ms={crossfade_ms}, ~{total_frames/float(rate):.3f}s')


if __name__ == '__main__':
    main()
