#!/usr/bin/env python3
"""
Generate a crossfaded repeat WAV using a linear crossfade window.
This complements the existing `extend_and_crossfade.py` which now uses
raised-cosine; we write linear variants so we can compare them.
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
    p.add_argument('--in', dest='infile', required=True)
    p.add_argument('--out', dest='outfile', required=True)
    p.add_argument('--seconds', type=float, default=1.0)
    p.add_argument('--rate', type=int, default=44100)
    p.add_argument('--channels', type=int, default=2)
    p.add_argument('--sampwidth', type=int, default=2)
    p.add_argument('--crossfade-ms', type=float, default=6.0)
    args = p.parse_args()

    infile = args.infile
    outfile = args.outfile
    rate = args.rate
    channels = args.channels
    sampwidth = args.sampwidth
    target_seconds = args.seconds
    crossfade_ms = args.crossfade_ms

    if sampwidth != 2:
        raise SystemExit('Only 16-bit supported')

    with open(infile, 'rb') as f:
        data = f.read()

    frame_bytes = channels * sampwidth
    full_frames = len(data) // frame_bytes
    if full_frames == 0:
        raise SystemExit('Input too short')

    data = data[: full_frames * frame_bytes]
    samples = array('h')
    samples.frombytes(data)

    frames_per_chunk = full_frames
    target_frames = int(math.ceil(target_seconds * rate))

    crossfade_frames = int((float(rate) * float(crossfade_ms)) / 1000.0)
    if crossfade_frames < 1:
        crossfade_frames = 1
    max_crossfade = max(1, frames_per_chunk // 2)
    if crossfade_frames > max_crossfade:
        crossfade_frames = max_crossfade

    if target_frames <= frames_per_chunk:
        repeats = 1
    else:
        per_repeat_add = frames_per_chunk - crossfade_frames
        if per_repeat_add <= 0:
            per_repeat_add = 1
        additional_needed = target_frames - frames_per_chunk
        repeats = 1 + int(math.ceil(float(additional_needed) / float(per_repeat_add)))

    out = array('h')
    chunk = samples
    crossfade_len_samples = crossfade_frames * channels

    for r in range(repeats):
        if r == 0:
            out.extend(chunk)
        else:
            out_tail_start = len(out) - crossfade_len_samples
            for k in range(crossfade_frames):
                fade_out = float(crossfade_frames - k) / float(crossfade_frames)
                fade_in = float(k) / float(crossfade_frames)
                for ch in range(channels):
                    out_idx = out_tail_start + k * channels + ch
                    chunk_idx = k * channels + ch
                    mixed = int(round(out[out_idx] * fade_out + chunk[chunk_idx] * fade_in))
                    out[out_idx] = clamp_i16(mixed)
            out.extend(chunk[crossfade_len_samples:])

    total_frames = len(out) // channels
    os.makedirs(os.path.dirname(outfile), exist_ok=True)
    with wave.open(outfile, 'wb') as w:
        w.setnchannels(channels)
        w.setsampwidth(sampwidth)
        w.setframerate(rate)
        w.writeframes(out.tobytes())

    print(f'Wrote {outfile}: {total_frames} frames, repeats={repeats}, crossfade_ms={crossfade_ms}')


if __name__ == '__main__':
    main()
