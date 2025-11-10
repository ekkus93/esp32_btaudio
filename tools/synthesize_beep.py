#!/usr/bin/env python3
"""
Synthesize a sine-tone WAV using the same parameters as the firmware's
worker/fallback generators (1 kHz, 16-bit, stereo, linear fade-in/out).

Usage:
  python3 tools/synthesize_beep.py --out build/worker_synth_1s.wav --seconds 1.0

Options include sample rate, channels, amplitude, tone frequency, and fade ms.
Defaults are tuned to match `audio_processor.c` (tone_hz=1000, amp=30000,
fade_ms=8, 44100Hz, stereo, 16-bit).
"""
import argparse
import os
import wave
import struct
import math

def synthesize(path, seconds=1.0, rate=44100, channels=2, sampwidth=2,
               tone_hz=1000, amp=30000.0, fade_ms=8):
    frame_bytes = channels * sampwidth
    total_frames = int(seconds * rate)
    two_pi = 2.0 * math.pi
    phase = 0.0
    phase_inc = (two_pi * float(tone_hz)) / float(rate)

    fade_frames = int((float(rate) * float(fade_ms)) / 1000.0)
    if fade_frames < 1:
        fade_frames = 1

    # Clip amplitude to int16 range
    max_amp = (1 << (sampwidth * 8 - 1)) - 1
    if amp > max_amp:
        amp = float(max_amp)

    samples = []
    for i in range(total_frames):
        env = 1.0
        # fade in
        if i < fade_frames:
            env = float(i) / float(fade_frames)
        # fade out
        elif i >= total_frames - fade_frames:
            tail_idx = total_frames - i
            if tail_idx < fade_frames:
                env = float(tail_idx) / float(fade_frames)

        v = math.sin(phase) * amp * env
        # clamp
        if v > max_amp: v = max_amp
        if v < -max_amp-1: v = -max_amp-1
        # pack per channel
        if sampwidth == 2:
            i16 = int(round(v))
            for ch in range(channels):
                samples.append(struct.pack('<h', i16))
        else:
            # support only 16-bit for now
            i16 = int(round(v))
            for ch in range(channels):
                samples.append(struct.pack('<h', i16))

        phase += phase_inc
        if phase >= two_pi:
            phase -= two_pi

    data = b''.join(samples)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, 'wb') as w:
        w.setnchannels(channels)
        w.setsampwidth(sampwidth)
        w.setframerate(rate)
        w.writeframes(data)

    print(f"Wrote {path}: {total_frames} frames, ~{total_frames/float(rate):.3f}s, {channels}ch @ {rate}Hz")

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--out', required=True, help='Output wav path')
    p.add_argument('--seconds', type=float, default=1.0, help='Duration in seconds')
    p.add_argument('--rate', type=int, default=44100, help='Sample rate (Hz)')
    p.add_argument('--channels', type=int, default=2, help='Channels')
    p.add_argument('--sampwidth', type=int, default=2, help='Bytes per sample (2=16-bit)')
    p.add_argument('--tone', type=float, default=1000.0, help='Tone frequency (Hz)')
    p.add_argument('--amp', type=float, default=30000.0, help='Amplitude (max for 16-bit ~32767)')
    p.add_argument('--fade-ms', type=float, default=8.0, help='Fade-in/out in milliseconds')
    args = p.parse_args()

    synthesize(args.out, seconds=args.seconds, rate=args.rate,
               channels=args.channels, sampwidth=args.sampwidth,
               tone_hz=args.tone, amp=args.amp, fade_ms=args.fade_ms)

if __name__ == '__main__':
    main()
