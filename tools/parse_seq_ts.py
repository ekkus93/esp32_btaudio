#!/usr/bin/env python3
import re
import csv
from statistics import mean

import sys

# CLI parsing with explicit units (auto/us/ms)
import argparse

parser = argparse.ArgumentParser(description='Parse SEQ/TS from event output. TS may be in microseconds or milliseconds.')
parser.add_argument('input', nargs='?', default='tmp/dump_event_stress_output.txt')
parser.add_argument('output', nargs='?', default='tmp/dump_event_stress_output.csv')
parser.add_argument('--units', choices=['auto', 'us', 'ms'], default='auto', help='Interpret TS as microseconds (us) or milliseconds (ms), or auto-detect.')
args = parser.parse_args()
INPUT = args.input
OUTPUT = args.output
UNITS = args.units

seq_re = re.compile(r'SEQ=(\d+)')
ts_re = re.compile(r'TS=(\d+)')

# Read lines and collect raw TS values (as ints) so we can decide units
rows_raw = []  # seq, raw_ts, line
with open(INPUT, 'r') as f:
    for line in f:
        line = line.strip()
        if 'DEVICE_FOUND' in line:
            s = seq_re.search(line)
            t = ts_re.search(line)
            if s and t:
                seq = int(s.group(1))
                raw_ts = int(t.group(1))
                rows_raw.append((seq, raw_ts, line))

if not rows_raw:
    print(f"No DEVICE_FOUND lines with SEQ/TS found in {INPUT}")
    sys.exit(1)

# Decide units
raw_ts_values = [r for _, r, _ in rows_raw]
def median(lst):
    s = sorted(lst)
    n = len(s)
    if n % 2 == 1:
        return s[n//2]
    return (s[n//2 - 1] + s[n//2]) / 2

detected_units = 'us'
if UNITS == 'us':
    detected_units = 'us'
elif UNITS == 'ms':
    detected_units = 'ms'
else:
    med = median(raw_ts_values)
    # Heuristic by magnitude: ms epoch ~1e12, us epoch ~1e15 (approx)
    if med > 1e13:
        detected_units = 'us'
    elif med > 1e11 and med <= 1e13:
        # ambiguous zone: check median delta magnitude
        deltas = [abs(b - a) for a, b in zip(raw_ts_values, raw_ts_values[1:])]
        if deltas:
            med_del = median(deltas)
            # if median delta ~1000 -> likely us (1ms = 1000us); if ~1-2 -> ms
            if med_del > 100:
                detected_units = 'us'
            else:
                detected_units = 'ms'
        else:
            detected_units = 'us'
    else:
        # small magnitude values (not epoch) — assume microseconds unless deltas indicate ms
        deltas = [abs(b - a) for a, b in zip(raw_ts_values, raw_ts_values[1:])]
        if deltas and median(deltas) <= 10:
            detected_units = 'ms'
        else:
            detected_units = 'us'

# Convert raw TS to microseconds internally
rows = []
for seq, raw_ts, line in rows_raw:
    if detected_units == 'ms':
        ts_us = int(raw_ts) * 1000
    else:
        ts_us = int(raw_ts)
    rows.append((seq, ts_us, line))

print(f"Interpreting TS as: {detected_units} (converted to microseconds)")

# Sort by appearance (they already are), but ensure stable order
rows_sorted = rows

# Compute deltas
deltas = []
prev_seq = None
prev_ts = None
anomalies = []
for seq, ts, line in rows_sorted:
    if prev_seq is not None:
        del_seq = seq - prev_seq
        del_ts = ts - prev_ts
        deltas.append((del_seq, del_ts))
        if del_seq != 1:
            anomalies.append(f"Non-consecutive SEQ: {prev_seq} -> {seq}")
        if del_ts < 0:
            anomalies.append(f"TS decreased: {prev_ts} -> {ts} at SEQ {seq}")
    prev_seq = seq
    prev_ts = ts

# Write CSV: seq, ts_us, delta_seq, delta_ts_us, raw_line
with open(OUTPUT, 'w', newline='') as csvfile:
    writer = csv.writer(csvfile)
    writer.writerow(['seq','ts_us','delta_seq','delta_ts_us','raw_line'])
    prev_seq = None
    prev_ts = None
    for seq, ts, line in rows_sorted:
        if prev_seq is None:
            writer.writerow([seq, ts, '', '', line])
        else:
            writer.writerow([seq, ts, seq-prev_seq, ts-prev_ts, line])
        prev_seq = seq
        prev_ts = ts

# Summary
count = len(rows_sorted)
print(f"Parsed {count} DEVICE_FOUND events from {INPUT}")
if count >= 2:
    seq_deltas = [d for d, _ in deltas]
    ts_deltas = [dt for _, dt in deltas]
    # ts_deltas are in microseconds; provide ms summary too
    ts_deltas_ms = [dt/1000.0 for dt in ts_deltas]
    print(f"SEQ deltas: min={min(seq_deltas)}, max={max(seq_deltas)}, avg={mean(seq_deltas):.2f}")
    print(f"TS deltas: min={min(ts_deltas)}us, max={max(ts_deltas)}us, avg={mean(ts_deltas):.2f}us")
    print(f"TS deltas (ms): min={min(ts_deltas_ms):.3f}ms, max={max(ts_deltas_ms):.3f}ms, avg={mean(ts_deltas_ms):.3f}ms")
else:
    print("Not enough events to compute deltas")

if anomalies:
    print("Anomalies detected:")
    for a in anomalies:
        print(' -', a)
else:
    print("No anomalies detected: SEQ strictly increasing by 1 and TS non-decreasing.")

print(f"CSV written to {OUTPUT}")
