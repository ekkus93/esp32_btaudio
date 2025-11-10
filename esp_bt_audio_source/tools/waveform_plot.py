#!/usr/bin/env python3
"""
Plot waveform PNGs for worker_long.wav and fallback_long.wav
"""
import wave
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / 'test_app' / 'build'

WAV_FILES = ['worker_long.wav', 'fallback_long.wav']

for name in WAV_FILES:
    wav_path = BUILD_DIR / name
    if not wav_path.exists():
        print(f"Skipping {name}: not found at {wav_path}")
        continue
    with wave.open(str(wav_path), 'rb') as wf:
        nchan = wf.getnchannels()
        sampwidth = wf.getsampwidth()
        fr = wf.getframerate()
        nframes = wf.getnframes()
        raw = wf.readframes(nframes)

    dtype = None
    if sampwidth == 1:
        dtype = np.uint8  # 8-bit unsigned
    elif sampwidth == 2:
        dtype = np.int16
    elif sampwidth == 3:
        # 24-bit packed - convert to int32
        a = np.frombuffer(raw, dtype=np.uint8)
        # reshape to (nframes*nchan, 3)
        a = a.reshape(-1, 3)
        # little-endian 24-bit -> int32
        ints = (a[:,0].astype(np.int32) | (a[:,1].astype(np.int32) << 8) | (a[:,2].astype(np.int32) << 16))
        # sign correction
        mask = ints & 0x800000
        ints = ints - (mask << 1)
        data = ints.reshape(-1, nchan)
    elif sampwidth == 4:
        dtype = np.int32
    else:
        raise RuntimeError(f'Unsupported sample width: {sampwidth}')

    if sampwidth in (1,2,4):
        data = np.frombuffer(raw, dtype=dtype)
        if nchan > 1:
            data = data.reshape(-1, nchan)
        else:
            data = data.reshape(-1, 1)

    # Prepare time axis
    frames = data.shape[0]
    t = np.linspace(0, frames / fr, num=frames)

    # Plot
    fig, ax = plt.subplots(figsize=(10,3))
    # If stereo, plot left and right in separate colors with slight alpha
    if data.shape[1] == 1:
        ax.plot(t, data[:,0], color='C0', linewidth=0.8)
    else:
        ax.plot(t, data[:,0], color='C0', linewidth=0.8, label='L')
        ax.plot(t, data[:,1], color='C1', linewidth=0.8, label='R')
        ax.legend(loc='upper right')

    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Amplitude')
    ax.set_title(name)
    ax.grid(True, alpha=0.25)
    plt.tight_layout()

    out_png = wav_path.with_suffix('.png')
    fig.savefig(str(out_png), dpi=200)
    plt.close(fig)
    print(f'Wrote {out_png} ({frames} frames @ {fr} Hz, {nchan} ch, {sampwidth*8}-bit)')
