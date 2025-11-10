#!/usr/bin/env python3
"""
Plot two WAV files side-by-side (original vs normalized) and save PNG.

Usage:
  python3 plot_waveforms_side_by_side.py original.wav normalized.wav out.png

This script depends on matplotlib and numpy. It intentionally keeps
dependencies minimal; both are common in dev environments. If unavailable,
install with: pip3 install matplotlib numpy
"""
import sys
import wave
import struct
import numpy as np
import matplotlib.pyplot as plt


def read_mono_samples(path):
    with wave.open(path, 'rb') as wf:
        nchannels = wf.getnchannels()
        sampwidth = wf.getsampwidth()
        fr = wf.getframerate()
        nframes = wf.getnframes()
        raw = wf.readframes(nframes)
    if sampwidth != 2:
        raise SystemExit('Only 16-bit WAV supported')
    fmt = '<' + ('h' * (len(raw)//2))
    samples = np.array(struct.unpack(fmt, raw), dtype=np.int16)
    if nchannels > 1:
        samples = samples.reshape(-1, nchannels)[:,0]
    times = np.arange(len(samples)) / float(fr)
    return times, samples, fr


def plot_pair(path_a, path_b, out_png, title_a='Original', title_b='Normalized'):
    ta, sa, fra = read_mono_samples(path_a)
    tb, sb, frb = read_mono_samples(path_b)
    # Use the common sample rate if equal, otherwise plot using each timebase
    fig, axes = plt.subplots(2, 1, figsize=(10, 5), sharex=False)

    axes[0].plot(ta, sa.astype(np.float32)/32768.0, color='C0')
    axes[0].set_title(title_a)
    axes[0].set_ylabel('Amplitude')
    axes[0].set_ylim(-1.05, 1.05)
    axes[0].grid(True, linewidth=0.5)

    axes[1].plot(tb, sb.astype(np.float32)/32768.0, color='C1')
    axes[1].set_title(title_b)
    axes[1].set_xlabel('Time (s)')
    axes[1].set_ylabel('Amplitude')
    axes[1].set_ylim(-1.05, 1.05)
    axes[1].grid(True, linewidth=0.5)

    fig.suptitle(f'Waveform comparison\n{out_png}')
    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    fig.savefig(out_png, dpi=200)
    print(f'Wrote {out_png}')


def main():
    if len(sys.argv) != 4:
        print('Usage: plot_waveforms_side_by_side.py original.wav normalized.wav out.png')
        sys.exit(2)
    a, b, out = sys.argv[1:4]
    plot_pair(a, b, out)


if __name__ == '__main__':
    main()
