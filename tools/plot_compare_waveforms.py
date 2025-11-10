#!/usr/bin/env python3
"""
Plot comparison PNGs for linear vs raised-cosine crossfades.

Usage:
  python3 tools/plot_compare_waveforms.py \
    --linear worker_long_1s_linear.wav --cos worker_long_1s_xfade.wav \
    --out worker_compare.png

This script reads two WAV files, computes a short zoomed window (first 2000 samples)
and a full-waveform downsampled overview, and writes a side-by-side PNG showing
both waveforms for quick visual comparison.
"""
import argparse
import wave
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import os


def read_wav(path):
    with wave.open(path, 'rb') as w:
        nch = w.getnchannels()
        sw = w.getsampwidth()
        sr = w.getframerate()
        n = w.getnframes()
        data = w.readframes(n)
    arr = np.frombuffer(data, dtype=np.int16)
    if nch > 1:
        arr = arr.reshape(-1, nch)
        # take left channel for plotting
        arr = arr[:,0]
    return arr.astype(np.float32), sr


def plot_pair(linear_path, cos_path, out_png, zoom_frames=2000):
    a_lin, sr = read_wav(linear_path)
    a_cos, _ = read_wav(cos_path)

    # Normalize for visual comparability
    def norm(x):
        m = np.max(np.abs(x))
        return x / (m + 1e-9)

    a_lin_n = norm(a_lin)
    a_cos_n = norm(a_cos)

    # Prepare figure: top zoom, bottom full-overview (downsampled)
    fig, axes = plt.subplots(2, 1, figsize=(10, 6))

    # Zoom region (start)
    end = min(len(a_lin_n), zoom_frames)
    t = np.arange(end) / float(sr)
    axes[0].plot(t, a_lin_n[:end], label='linear', alpha=0.8)
    axes[0].plot(t, a_cos_n[:end], label='raised-cosine', alpha=0.8)
    axes[0].set_title('Zoom (first {:.0f} samples, {:.3f}s)'.format(end, float(end)/sr))
    axes[0].legend(loc='upper right')
    axes[0].set_ylabel('Normalized')

    # Full overview: downsample to <= 2000 points using simple decimation
    def downsample(x, pts=2000):
        if len(x) <= pts:
            return x
        factor = int(np.ceil(len(x) / float(pts)))
        xs = x[:(len(x)//factor)*factor].reshape(-1, factor)
        return xs.mean(axis=1)

    lin_ds = downsample(a_lin_n)
    cos_ds = downsample(a_cos_n)
    t2 = np.arange(len(lin_ds)) * (len(a_lin_n) / float(len(lin_ds))) / float(sr)
    axes[1].plot(t2, lin_ds, label='linear', alpha=0.8)
    axes[1].plot(t2, cos_ds, label='raised-cosine', alpha=0.8)
    axes[1].set_title('Overview (downsampled)')
    axes[1].set_xlabel('Time (s)')
    axes[1].set_ylabel('Normalized')
    axes[1].legend(loc='upper right')

    plt.tight_layout()
    os.makedirs(os.path.dirname(out_png) or '.', exist_ok=True)
    fig.savefig(out_png, dpi=150)
    print(f'Wrote {out_png}')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--linear', required=True)
    p.add_argument('--cos', required=True)
    p.add_argument('--out', required=True)
    args = p.parse_args()
    plot_pair(args.linear, args.cos, args.out)


if __name__ == '__main__':
    main()
