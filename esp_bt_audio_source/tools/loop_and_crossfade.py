#!/usr/bin/env python3
"""
Loop and crossfade a 16-bit WAV to make a longer, listenable sample.

Usage:
  python3 loop_and_crossfade.py in.wav out.wav --repeats 8 --crossfade-ms 50

Defaults: repeats=8, crossfade-ms=50

Dependencies: numpy (pip3 install numpy)
"""
import argparse
import wave
import struct
import numpy as np


def read_wav_frames(path):
    with wave.open(path, 'rb') as wf:
        nch, sw, fr, nframes = wf.getnchannels(), wf.getsampwidth(), wf.getframerate(), wf.getnframes()
        if sw != 2:
            raise SystemExit('Only 16-bit WAV supported')
        raw = wf.readframes(nframes)
    fmt = '<' + ('h' * (len(raw)//2))
    samples = np.array(struct.unpack(fmt, raw), dtype=np.int16)
    samples = samples.reshape(-1, nch)
    return samples, nch, fr


def write_wav_frames(path, samples, nch, fr):
    samples = samples.astype(np.int16)
    flat = samples.reshape(-1)
    fmt = '<' + ('h' * flat.size)
    raw = struct.pack(fmt, *flat.tolist())
    with wave.open(path, 'wb') as wf:
        wf.setnchannels(nch)
        wf.setsampwidth(2)
        wf.setframerate(fr)
        wf.writeframes(raw)


def loop_and_crossfade(samples, repeats, crossfade_samples):
    if repeats <= 1:
        return samples.copy()
    # Prepare output buffer
    out = samples.copy()
    for i in range(1, repeats):
        # next block
        nxt = samples.copy()
        if crossfade_samples > 0:
            # ensure we don't exceed length
            cf = min(crossfade_samples, out.shape[0], nxt.shape[0])
            if cf > 0:
                # apply linear crossfade on cf samples
                fade_out = np.linspace(1.0, 0.0, cf, endpoint=False)
                fade_in = np.linspace(0.0, 1.0, cf, endpoint=False)
                # last cf of out and first cf of nxt
                out_tail = out[-cf:, :].astype(np.float32)
                nxt_head = nxt[:cf, :].astype(np.float32)
                mixed = (out_tail * fade_out[:, None]) + (nxt_head * fade_in[:, None])
                # replace tail and head
                out[-cf:, :] = np.round(mixed).astype(np.int16)
                nxt[:cf, :] = np.round(nxt_head * (1.0)).astype(np.int16)
        # append the next block after overlap
        out = np.vstack([out, nxt[crossfade_samples:, :]])
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument('infile')
    p.add_argument('outfile')
    p.add_argument('--repeats', type=int, default=8)
    p.add_argument('--crossfade-ms', type=float, default=50.0)
    args = p.parse_args()

    samples, nch, fr = read_wav_frames(args.infile)
    crossfade_samples = int((args.crossfade_ms / 1000.0) * fr)
    # Cap crossfade to half of sample length to avoid negative size
    max_cf = samples.shape[0] // 2
    if crossfade_samples > max_cf:
        crossfade_samples = max_cf
    print(f'Input frames={samples.shape[0]} channels={nch} framerate={fr}')
    print(f'Repeats={args.repeats} crossfade_samples={crossfade_samples}')

    out = loop_and_crossfade(samples, args.repeats, crossfade_samples)
    print(f'Output frames={out.shape[0]} (approx {out.shape[0]/fr:.2f}s)')
    write_wav_frames(args.outfile, out, nch, fr)
    print(f'Wrote {args.outfile}')


if __name__ == '__main__':
    main()
