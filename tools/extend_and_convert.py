#!/usr/bin/env python3
"""
Repeat/concatenate a short raw PCM .bin capture to reach a target duration
and write a valid WAV file. Assumes 16-bit signed little-endian samples.

Usage examples:
  python3 tools/extend_and_convert.py \
    --in esp_bt_audio_source/test_app/build/worker_long.bin \
    --out esp_bt_audio_source/test_app/build/worker_long_1s.wav \
    --seconds 1.0

This script trims any trailing partial frame in the input .bin and repeats
whole frames as needed to reach (or slightly exceed) the requested duration.
"""
import argparse
import os
import math
import wave

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--in", dest="infile", required=True,
                   help="Input raw .bin file (PCM 16-bit LE, interleaved)")
    p.add_argument("--out", dest="outfile", required=True,
                   help="Output .wav file path to create")
    p.add_argument("--seconds", dest="seconds", type=float, default=1.0,
                   help="Target minimum duration in seconds (default: 1.0)")
    p.add_argument("--rate", dest="rate", type=int, default=44100,
                   help="Sample rate in Hz (default: 44100)")
    p.add_argument("--channels", dest="channels", type=int, default=2,
                   help="Number of channels (default: 2)")
    p.add_argument("--sampwidth", dest="sampwidth", type=int, default=2,
                   help="Sample width in bytes (default: 2 -> 16-bit)")

    args = p.parse_args()

    infile = args.infile
    outfile = args.outfile
    rate = args.rate
    channels = args.channels
    sampwidth = args.sampwidth
    target_seconds = args.seconds

    if not os.path.isfile(infile):
        raise SystemExit(f"Input file not found: {infile}")

    with open(infile, "rb") as f:
        data = f.read()

    frame_bytes = channels * sampwidth
    if len(data) == 0:
        raise SystemExit("Input file is empty")

    # Trim any trailing partial frame
    full_frames = len(data) // frame_bytes
    if full_frames == 0:
        raise SystemExit("Input file does not contain a full audio frame")

    data = data[: full_frames * frame_bytes]

    seconds_per_chunk = full_frames / float(rate)
    if seconds_per_chunk <= 0:
        raise SystemExit("Invalid chunk length (0 seconds)")

    repeats = max(1, math.ceil(target_seconds / seconds_per_chunk))

    repeated = data * repeats
    total_frames = full_frames * repeats

    # Write WAV
    os.makedirs(os.path.dirname(outfile), exist_ok=True)
    with wave.open(outfile, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(sampwidth)
        w.setframerate(rate)
        w.writeframes(repeated)

    print(f"Wrote {outfile}: {total_frames} frames, {repeats} repeats, ~{total_frames/float(rate):.3f}s")

if __name__ == "__main__":
    main()
