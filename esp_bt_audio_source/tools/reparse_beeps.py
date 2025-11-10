#!/usr/bin/env python3
"""
Re-scan all build logs for DIAG:worker-out: and DIAG:fallback-out: lines,
concatenate the hex bytes into worker_long.bin and fallback_long.bin,
then write a side-by-side hexdump and a small diff report.
"""
import glob
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / 'test_app' / 'build'
OUT_DIR.mkdir(parents=True, exist_ok=True)

worker_bytes = bytearray()
fallback_bytes = bytearray()
worker_rows = 0
fallback_rows = 0
scanned_files = []

diag_worker_tag = 'DIAG:worker-out:'
diag_fallback_tag = 'DIAG:fallback-out:'

# collect .log files under the tree (including nested build/ dirs)
log_paths = sorted(Path(ROOT).rglob('build/*.log'))
for p in log_paths:
    scanned_files.append(str(p))
    try:
        with p.open('r', errors='ignore') as fh:
            for line in fh:
                if diag_worker_tag in line:
                    worker_rows += 1
                    # extract tokens after tag
                    tail = line.split(diag_worker_tag, 1)[1].strip()
                    if not tail:
                        continue
                    toks = tail.split()
                    for t in toks:
                        try:
                            worker_bytes.append(int(t, 16) & 0xFF)
                        except Exception:
                            # skip non-hex
                            pass
                if diag_fallback_tag in line:
                    fallback_rows += 1
                    tail = line.split(diag_fallback_tag, 1)[1].strip()
                    if not tail:
                        continue
                    toks = tail.split()
                    for t in toks:
                        try:
                            fallback_bytes.append(int(t, 16) & 0xFF)
                        except Exception:
                            pass
    except Exception as e:
        print(f'Warning: failed to read {p}: {e}', file=sys.stderr)

# Write binaries
worker_bin = OUT_DIR / 'worker_long.bin'
fallback_bin = OUT_DIR / 'fallback_long.bin'
with worker_bin.open('wb') as fh:
    fh.write(worker_bytes)
with fallback_bin.open('wb') as fh:
    fh.write(fallback_bytes)

# Hexdump side-by-side
hexdump_path = OUT_DIR / 'hexdump_side_by_side.txt'
max_len = max(len(worker_bytes), len(fallback_bytes))
lines = []
lines.append(f'Scanned {len(scanned_files)} log files\n')
lines.append('Files scanned:\n')
for f in scanned_files:
    lines.append(f' - {f}\n')
lines.append('\n')
lines.append(f'Worker rows found: {worker_rows}  total bytes: {len(worker_bytes)}\n')
lines.append(f'Fallback rows found: {fallback_rows} total bytes: {len(fallback_bytes)}\n')
lines.append('\n')
lines.append('Offset  Worker  Fallback  Note\n')
lines.append('------  ------  --------  ----\n')

diffs = []
for i in range(0, max_len):
    wb = worker_bytes[i] if i < len(worker_bytes) else None
    fb = fallback_bytes[i] if i < len(fallback_bytes) else None
    w_hex = f'{wb:02x}' if wb is not None else '--'
    f_hex = f'{fb:02x}' if fb is not None else '--'
    note = ''
    if wb is None and fb is None:
        note = 'both-missing'
    elif wb != fb:
        note = 'DIFF'
        diffs.append((i, w_hex, f_hex))
    lines.append(f'{i:06d}  {w_hex}    {f_hex}     {note}\n')

with hexdump_path.open('w') as fh:
    fh.writelines(lines)

# Diff report
diff_report_path = OUT_DIR / 'hex_diff_report.txt'
with diff_report_path.open('w') as fh:
    fh.write(f'Worker rows: {worker_rows}, bytes: {len(worker_bytes)}\n')
    fh.write(f'Fallback rows: {fallback_rows}, bytes: {len(fallback_bytes)}\n')
    fh.write(f'Differences found: {len(diffs)}\n')
    fh.write('\n')
    if diffs:
        fh.write('First 200 differences (offset, worker, fallback):\n')
        for i, (off, w, f) in enumerate(diffs[:200]):
            fh.write(f'{off:06d}  {w}  {f}\n')

# Print summary
print('Re-parse complete')
print(f'Logs scanned: {len(scanned_files)}')
print(f'Worker rows: {worker_rows}  bytes: {len(worker_bytes)}  -> {worker_bin}')
print(f'Fallback rows: {fallback_rows} bytes: {len(fallback_bytes)}  -> {fallback_bin}')
print(f'Hexdump: {hexdump_path}')
print(f'Diff report: {diff_report_path}')
if len(diffs) == 0:
    print('No byte differences found between worker_long.bin and fallback_long.bin')
else:
    print(f'{len(diffs)} differing byte positions; see {diff_report_path}')

# Exit code non-zero if no DIAG rows found at all
if worker_rows == 0 and fallback_rows == 0:
    print('Warning: no DIAG rows found in scanned logs', file=sys.stderr)
    sys.exit(2)
else:
    sys.exit(0)
