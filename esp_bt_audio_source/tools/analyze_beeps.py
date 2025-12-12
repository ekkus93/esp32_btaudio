#!/usr/bin/env python3
"""
Simple beep/tone burst detector for recorded WAV files.
- Reads `tmp/play_audio.wav` (mono or stereo S16_LE)
- Computes short-window RMS (default 10 ms)
- Detects bursts where RMS > median + k * std (k=4 default)
- Merges close bursts and outputs start/end times (ms) relative to WAV start
- Reads `tmp/play_start_ts.txt` (epoch ms) and outputs epoch-ms timestamps
- Correlates each burst with lines from `tmp/play_serial.log` (timestamped logs in same epoch-ms format)
- Writes `tmp/beep_events.csv` and prints a short report

Usage: python3 tools/analyze_beeps.py
"""
import wave
import audioop
import argparse
import os
import sys
import math

def read_start_ts(path):
    try:
        with open(path, 'r') as f:
            s = f.read().strip()
            return int(s)
    except Exception as e:
        print(f"Failed to read start ts from {path}: {e}")
        return None


def read_serial_lines(path):
    if not os.path.exists(path):
        return []
    lines = []
    with open(path, 'r', errors='ignore') as f:
        for ln in f:
            ln = ln.rstrip('\n')
            if not ln:
                continue
            # Expect lines starting with epoch_ms then a space
            parts = ln.split(' ', 1)
            try:
                ts = int(parts[0])
                msg = parts[1] if len(parts) > 1 else ''
                lines.append((ts, msg))
            except Exception:
                # fallback: store as msg with ts=None
                lines.append((None, ln))
    return lines


def detect_bursts(wav_path, window_ms=10, thr_k=4, merge_gap_ms=50):
    if not os.path.exists(wav_path):
        raise FileNotFoundError(wav_path)
    wf = wave.open(wav_path, 'rb')
    nch = wf.getnchannels()
    sr = wf.getframerate()
    sampwidth = wf.getsampwidth()
    nframes = wf.getnframes()
    duration_ms = int(nframes / sr * 1000)
    window_size = max(1, int(sr * window_ms / 1000))
    # read full file
    raw = wf.readframes(nframes)
    wf.close()
    # convert to mono if needed
    if nch == 2:
        raw = audioop.tomono(raw, sampwidth, 0.5, 0.5)
        nch = 1
    # iterate windows
    rms_list = []
    offsets = []
    pos = 0
    total_frames = nframes
    frame_bytes = sampwidth * nch
    frames_read = 0
    while frames_read < total_frames:
        take = min(window_size, total_frames - frames_read)
        start_byte = frames_read * frame_bytes
        end_byte = start_byte + take * frame_bytes
        chunk = raw[start_byte:end_byte]
        if len(chunk) == 0:
            break
        rms = audioop.rms(chunk, sampwidth)
        rms_list.append(rms)
        offsets.append(frames_read / sr * 1000.0)
        frames_read += take
    if not rms_list:
        return []
    # statistics
    import statistics
    median = statistics.median(rms_list)
    try:
        stdev = statistics.pstdev(rms_list)
    except Exception:
        stdev = 0.0
    threshold = median + thr_k * stdev if stdev > 0 else median * (1 + thr_k/10.0)
    # detect above-threshold windows
    above = [i for i, v in enumerate(rms_list) if v > threshold]
    bursts = []
    if above:
        # group contiguous indices
        start_i = above[0]
        last_i = above[0]
        for i in above[1:]:
            if i - last_i <= 1:
                last_i = i
            else:
                start_ms = offsets[start_i]
                end_ms = offsets[last_i] + window_ms
                bursts.append((start_ms, end_ms))
                start_i = i
                last_i = i
        # final
        start_ms = offsets[start_i]
        end_ms = offsets[last_i] + window_ms
        bursts.append((start_ms, end_ms))
    # merge close bursts
    merged = []
    for s,e in bursts:
        if not merged:
            merged.append([s,e])
        else:
            if s - merged[-1][1] <= merge_gap_ms:
                merged[-1][1] = max(merged[-1][1], e)
            else:
                merged.append([s,e])
    # compute peak rms inside each burst
    results = []
    for s,e in merged:
        # compute frames covering interval
        s_idx = int(s / window_ms)
        e_idx = int(math.ceil(e / window_ms))
        s_idx = max(0, s_idx)
        e_idx = min(len(rms_list), e_idx)
        peak = max(rms_list[s_idx:e_idx]) if e_idx > s_idx else 0
        results.append({'start_ms': s, 'end_ms': e, 'duration_ms': e-s, 'peak_rms': peak})
    return results


def correlate_with_log(events, serial_lines, window_ms=500):
    # serial_lines: list of (ts,msg)
    out = []
    for ev in events:
        ev_start = ev['epoch_start']
        matches = []
        for ts,msg in serial_lines:
            if ts is None:
                continue
            if abs(ts - ev_start) <= window_ms or (ts >= ev['epoch_start'] and ts <= ev['epoch_end']):
                matches.append((ts, msg, ts - ev_start))
        out.append(matches)
    return out


def write_csv(events, out_path):
    import csv
    with open(out_path, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['event_id','start_ms_rel','end_ms_rel','duration_ms','epoch_start_ms','epoch_end_ms','peak_rms'])
        for i,ev in enumerate(events):
            w.writerow([i, f"{ev['start_ms']:.1f}", f"{ev['end_ms']:.1f}", f"{ev['duration_ms']:.1f}", ev['epoch_start'], ev['epoch_end'], ev['peak_rms']])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--wav', default='tmp/play_audio.wav')
    parser.add_argument('--start-ts', default='tmp/play_start_ts.txt')
    parser.add_argument('--serial-log', default='tmp/play_serial.log')
    parser.add_argument('--out-csv', default='tmp/beep_events.csv')
    parser.add_argument('--window-ms', type=int, default=10)
    parser.add_argument('--thr-k', type=float, default=4.0)
    parser.add_argument('--merge-gap-ms', type=int, default=50)
    parser.add_argument('--corr-window-ms', type=int, default=500)
    args = parser.parse_args()

    if not os.path.exists(args.wav):
        print(f"WAV file not found: {args.wav}")
        sys.exit(2)

    start_ts = read_start_ts(args.start_ts)
    if start_ts is None:
        print("Warning: start ts not found; event epoch timestamps will be left blank")

    serial_lines = read_serial_lines(args.serial_log)

    events = detect_bursts(args.wav, window_ms=args.window_ms, thr_k=args.thr_k, merge_gap_ms=args.merge_gap_ms)
    if not events:
        print("No bursts detected.")
        sys.exit(0)

    # attach epoch timestamps
    for ev in events:
        ev['epoch_start'] = int(start_ts + ev['start_ms']) if start_ts is not None else None
        ev['epoch_end'] = int(start_ts + ev['end_ms']) if start_ts is not None else None

    write_csv(events, args.out_csv)

    # correlate
    correlations = correlate_with_log(events, serial_lines, window_ms=args.corr_window_ms)

    # print report
    print(f"Detected {len(events)} beep/tone event(s). CSV written to {args.out_csv}")
    for i,(ev, matches) in enumerate(zip(events, correlations)):
        print('---')
        print(f"Event {i}: start={ev['start_ms']:.1f} ms rel, duration={ev['duration_ms']:.1f} ms, epoch_start={ev['epoch_start']}")
        if matches:
            print(f"  {len(matches)} matching serial lines within +/-{args.corr_window_ms} ms of event start:")
            for ts,msg,delta in matches:
                dd = '+'+str(delta) if delta>=0 else str(delta)
                print(f"    {ts} ({dd} ms): {msg}")
        else:
            print("  No nearby serial lines found within correlation window.")

if __name__ == '__main__':
    main()
