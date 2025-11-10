#!/usr/bin/env python3
"""
Adjust gain or normalize 16-bit WAV files to avoid clipping.

Usage:
  python3 adjust_wav_gain.py in.wav out.wav [--gain GAIN] [--peak P]

Defaults: normalize to peak=0.90 (90% of full scale). If --gain is
provided, multiplies samples by that factor instead of normalizing.

This tool is intentionally dependency-free (uses only stdlib).
"""
import sys
import wave
import struct
import argparse


def read_wav(path):
    with wave.open(path, 'rb') as wf:
        params = wf.getparams()
        nchannels, sampwidth, framerate, nframes = params[:4]
        if sampwidth != 2:
            raise SystemExit(f"Unsupported sample width: {sampwidth*8} bits (only 16-bit supported)")
        raw = wf.readframes(nframes)
    return nchannels, sampwidth, framerate, nframes, raw


def write_wav(path, nchannels, sampwidth, framerate, frames_bytes):
    with wave.open(path, 'wb') as wf:
        wf.setnchannels(nchannels)
        wf.setsampwidth(sampwidth)
        wf.setframerate(framerate)
        wf.writeframes(frames_bytes)


def scale_frames(raw_bytes, nchannels, gain):
    # raw_bytes is little-endian PCM16 interleaved
    fmt = '<' + ('h' * (len(raw_bytes) // 2))
    samples = list(struct.unpack(fmt, raw_bytes))
    max_val = 0
    for i in range(len(samples)):
        v = int(round(samples[i] * gain))
        # clip
        if v > 32767:
            v = 32767
        elif v < -32768:
            v = -32768
        samples[i] = v
        if abs(v) > max_val:
            max_val = abs(v)
    out = struct.pack(fmt, *samples)
    return out, max_val


def find_peak(raw_bytes):
    fmt = '<' + ('h' * (len(raw_bytes) // 2))
    samples = struct.unpack(fmt, raw_bytes)
    peak = max(abs(v) for v in samples) if samples else 0
    return peak


def main():
    p = argparse.ArgumentParser()
    p.add_argument('infile')
    p.add_argument('outfile')
    p.add_argument('--gain', type=float, default=None, help='Fixed linear gain multiplier (overrides normalize)')
    p.add_argument('--peak', type=float, default=0.90, help='Target peak fraction (0.0-1.0) when normalizing')
    args = p.parse_args()

    nchannels, sampwidth, framerate, nframes, raw = read_wav(args.infile)

    if args.gain is None:
        # normalize: compute current peak and scale to target
        peak = find_peak(raw)
        if peak == 0:
            print('Input is silent; copying unchanged')
            write_wav(args.outfile, nchannels, sampwidth, framerate, raw)
            return
        target = int(32767 * float(args.peak))
        gain = float(target) / float(peak)
        print(f'Normalizing: current_peak={peak}, target={target}, gain={gain:.4f}')
    else:
        gain = float(args.gain)
        print(f'Applying fixed gain: {gain:.4f}')

    out_bytes, out_peak = scale_frames(raw, nchannels, gain)
    print(f'Output peak after scaling: {out_peak}')
    write_wav(args.outfile, nchannels, sampwidth, framerate, out_bytes)


if __name__ == '__main__':
    main()
