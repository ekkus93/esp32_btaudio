#!/usr/bin/env python3
"""compare_bt_capture.py — verify UARTAUDIO end-to-end fidelity numerically.

Streams-and-captures workflow (laptop as A2DP sink):

    parec --device=bluez_source.<ESP32_MAC>.a2dp_source --raw           --format=s16le --rate=44100 --channels=2 > capture.raw &
    python tools/stream_audio_uart.py --port /dev/ttyUSB0 source.wav
    python tools/compare_bt_capture.py capture.raw source.wav

The reference is the source WAV (22050 stereo s16) pushed through the
firmware's exact 2x midpoint upsampler. Windowed cross-correlation then
tracks the capture/reference offset over time:
  - lossless playback = constant offset, r ~= 1.0 per window
  - each lost 1024 B UART frame = 512-sample (11.6 ms) forward skip
  - A2DP zero-fill shows up as quiet runs with rms_ratio << 1
Use non-repetitive source material (e.g. a frequency sweep) — repeating
patterns let the correlator lock onto the wrong cycle. Sub-sample
alignment limits per-window r on content above ~5 kHz; energy and
quiet-run columns distinguish artifacts from real defects.
"""
import sys, wave
import numpy as np

if len(sys.argv) < 3:
    sys.exit(__doc__)
CAPTURE = sys.argv[1]
SOURCE = sys.argv[2]
SR = 44100

# --- reference: load 22050 WAV, upsample exactly like the firmware ---
w = wave.open(SOURCE, "rb")
src = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2).astype(np.int32)
w.close()
prev = np.vstack([[0, 0], src[:-1]])
mid = (prev + src) // 2   # C-truncation equivalent for our positive-ish content
ref = np.empty((src.shape[0] * 2, 2), dtype=np.int32)
ref[0::2] = mid
ref[1::2] = src
ref_m = ref.mean(axis=1).astype(np.float64)

# --- capture ---
cap = np.frombuffer(open(CAPTURE, "rb").read(), dtype=np.int16)
cap = cap[: len(cap) // 2 * 2].reshape(-1, 2).astype(np.int32)
cap_m = cap.mean(axis=1).astype(np.float64)
print(f"reference: {len(ref_m)/SR:.2f}s  capture: {len(cap_m)/SR:.2f}s")

def xcorr_offset(needle, hay):
    """offset of needle in hay via FFT cross-correlation; returns (pos, score)"""
    n = len(needle) + len(hay)
    N = 1 << (n - 1).bit_length()
    C = np.fft.irfft(np.fft.rfft(hay, N) * np.conj(np.fft.rfft(needle, N)), N)
    C = C[: len(hay) - len(needle) + 1]
    pos = int(np.argmax(C))
    seg = hay[pos : pos + len(needle)]
    denom = np.linalg.norm(needle) * np.linalg.norm(seg)
    r = float(np.dot(needle, seg) / denom) if denom > 0 else 0.0
    return pos, r

# global start alignment: first 1 s of reference vs first 8 s of capture
start, r0 = xcorr_offset(ref_m[:SR], cap_m[: 8 * SR])
print(f"global alignment: capture starts at +{start/SR*1000:.1f} ms (r={r0:.4f})")

# windowed tracking
WIN = SR // 4          # 250 ms windows
SEARCH = 4096          # +/- ~46 ms local search
offsets, scores, times = [], [], []
prev_off = start
for wstart in range(0, len(ref_m) - WIN, WIN):
    needle = ref_m[wstart : wstart + WIN]
    lo = max(0, wstart + prev_off - SEARCH)
    hi = min(len(cap_m), wstart + prev_off + WIN + SEARCH)
    if hi - lo < WIN + 512:
        break
    pos, r = xcorr_offset(needle, cap_m[lo:hi])
    off = lo + pos - wstart
    times.append(wstart / SR)
    offsets.append(off)
    scores.append(r)
    if r > 0.5:
        prev_off = off

offsets = np.array(offsets); scores = np.array(scores); times = np.array(times)
good = scores > 0.9
print(f"\nwindows: {len(scores)}  correlation r>0.99: {(scores>0.99).sum()}  "
      f"r>0.9: {good.sum()}  median r: {np.median(scores):.4f}")

# skip detection: offset drops between consecutive well-matched windows
skips = []
for i in range(1, len(offsets)):
    if scores[i] > 0.9 and scores[i-1] > 0.9:
        d = offsets[i] - offsets[i-1]
        if d <= -256:  # capture lost >= half a frame of audio
            skips.append((times[i], -d))
if skips:
    total = sum(d for _, d in skips)
    print(f"\nSKIPS (audio excised from capture): {len(skips)} events, "
          f"{total} samples = {total/SR*1000:.1f} ms total")
    for t, d in skips:
        frames = d / 512
        print(f"  at {t:5.2f}s: {d:5d} samples ({d/SR*1000:5.1f} ms ~= {frames:.1f} lost UART frames)")
else:
    print("\nno skips detected")

# amplitude ratio (volume 80 scaling check) over well-matched windows
if good.any():
    i = int(np.argmax(scores))
    ws = int(times[i] * SR)
    seg = cap_m[ws + offsets[i] : ws + offsets[i] + WIN]
    ratio = np.linalg.norm(seg) / np.linalg.norm(ref_m[ws:ws+WIN])
    print(f"\namplitude ratio capture/reference: {ratio:.3f} (VOLUME 80 scaling)")

# where does quality degrade?
bad = np.where(scores < 0.9)[0]
if len(bad):
    print(f"degraded windows (r<0.9) at: {[f'{times[b]:.2f}s' for b in bad[:12]]}")

# --- degraded-window forensics: silence insertion vs corruption ---
print("\ndegraded-window detail (RMS ratio + zero-run scan):")
for b in bad[:14]:
    ws = int(times[b] * SR)
    seg = cap_m[ws + offsets[b] : ws + offsets[b] + WIN]
    rseg = ref_m[ws : ws + WIN]
    if len(seg) < WIN:
        continue
    rms_ratio = (np.sqrt((seg**2).mean()) + 1) / (np.sqrt((rseg**2).mean()) + 1)
    # longest run of near-zero capture samples in this window
    quiet = np.abs(seg) < 50
    runs = np.diff(np.flatnonzero(np.concatenate(([1], np.diff(quiet.astype(int)) != 0, [1]))))
    zrun = 0
    idx = 0
    for rl in runs:
        if quiet[idx]:
            zrun = max(zrun, rl)
        idx += rl
    print(f"  {times[b]:6.2f}s  r={scores[b]:.3f}  rms_ratio={rms_ratio:.2f}  "
          f"longest_quiet_run={zrun/SR*1000:6.1f} ms")
