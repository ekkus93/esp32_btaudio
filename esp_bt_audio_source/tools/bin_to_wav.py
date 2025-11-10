#!/usr/bin/env python3
"""
Convert worker_long.bin and fallback_long.bin to WAV using defaults found in the repo:
- sample_rate: 44100 (AUDIO_SAMPLE_RATE_44K)
- channels: 2 (AUDIO_CHANNEL_STEREO)
- bit depth: 16-bit signed little-endian

Writes worker_long.wav and fallback_long.wav next to the .bin files.
"""
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / 'test_app' / 'build'

FILES = ['worker_long.bin', 'fallback_long.bin']
SAMPLE_RATE = 44100
CHANNELS = 2
SAMPWIDTH = 2  # bytes (16-bit)

for name in FILES:
    bin_path = BUILD_DIR / name
    if not bin_path.exists():
        print(f"Skipping {name}: not found at {bin_path}")
        continue
    wav_path = bin_path.with_suffix('.wav')
    data = bin_path.read_bytes()
    # Trim to whole frames
    frame_bytes = SAMPWIDTH * CHANNELS
    if len(data) % frame_bytes != 0:
        trimmed = len(data) - (len(data) % frame_bytes)
        print(f"Trimming {name} from {len(data)} -> {trimmed} bytes to align frames")
        data = data[:trimmed]
    with wave.open(str(wav_path), 'wb') as wf:
        wf.setnchannels(CHANNELS)
        wf.setsampwidth(SAMPWIDTH)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(data)
    print(f"Wrote {wav_path} ({len(data)} bytes, {len(data)//frame_bytes} frames @ {SAMPLE_RATE} Hz, {CHANNELS} ch)")
