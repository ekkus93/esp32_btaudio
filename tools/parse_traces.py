#!/usr/bin/env python3
"""
Parse DIAG/TRACE allocation and audio diagnostic lines from log files.
Writes CSV and JSON summaries.

Usage: python tools/parse_traces.py <logfile1> [<logfile2> ...]
Outputs: tmp/trace_parsed.csv and tmp/trace_parsed.json
"""
import sys
import re
import json
import csv
import shutil
import subprocess
from pathlib import Path

# simple arg parsing: allow options --elf=<path> and --addr2line=<path>
if len(sys.argv) < 2:
    print("Usage: parse_traces.py [--elf=ELF] [--addr2line=ADDR2LINE] <logfile> [<logfile> ...]")
    sys.exit(1)

elf_path = None
addr2line_cmd = None
logfiles = []
for a in sys.argv[1:]:
    if a.startswith('--elf='):
        elf_path = a.split('=', 1)[1]
    elif a.startswith('--addr2line='):
        addr2line_cmd = a.split('=', 1)[1]
    else:
        logfiles.append(a)

# if addr2line not provided but ELF given, try to find addr2line on PATH
if elf_path and not addr2line_cmd:
    addr2line_cmd = shutil.which('addr2line') or shutil.which('xtensa-esp32-elf-addr2line')
entries = []

# regexes
# capture a timestamp like I (1238)
re_tick = re.compile(r"\bI \((\d+)\)")
re_diag_read = re.compile(r"DIAG-READ-AUDIO-ITEM:.*?ptr=(0x[0-9a-fA-F]+)\s+size=(\d+).*?free_before=(\d+)")
re_worker_enq = re.compile(r"DIAG-WORKER-ENQ:.*?len=(\d+)|attempt len=(\d+).*?free_before=(\d+)")
# better: look for 'attempt len=NUM free_before=NUM' or 'len=NUM free_before=NUM'
re_worker_enq2 = re.compile(r"DIAG-WORKER-ENQ:.*?(?:attempt\s+)?len=(?P<len>\d+).*?free_before=(?P<free_before>\d+).*?(?:synth=(?P<synth>\d+))?")
re_worker_ret = re.compile(r"DIAG-WORKER-RET:.*?len=(\d+).*?free_before=(\d+).*?free_after=(\d+)")
re_worker_ret2 = re.compile(r"DIAG-WORKER-RET:.*?len=(?P<len>\d+).*?free_before=(?P<free_before>\d+).*?free_after=(?P<free_after>\d+)")
re_trace = re.compile(r"TRACE:\s*(.*)")
re_diag_worker = re.compile(r"DIAG-WORKER-.*")
# malloc/heap diagnostics
re_malloc_usable = re.compile(r"malloc_usable_size\([^)]*\)\s*[=:\s]+(\d+)")
re_usable_alt = re.compile(r"usable_size\s*[=:\s]+(\d+)")
re_heap_free = re.compile(r"heap_caps_get_free_size\([^)]*\)\s*[=:\s]+(\d+)")
re_heap_largest = re.compile(r"heap_caps_get_largest_free_block\([^)]*\)\s*[=:\s]+(\d+)")
re_addr = re.compile(r"(0x[0-9a-fA-F]+)")

for path in logfiles:
    p = Path(path)
    if not p.exists():
        print(f"Skipping missing file: {path}")
        continue
    last_tick = None
    with p.open('r', errors='ignore') as f:
        for lineno, line in enumerate(f, start=1):
            line = line.rstrip('\n')
            m_tick = re_tick.search(line)
            if m_tick:
                last_tick = int(m_tick.group(1))
            # DIAG-READ-AUDIO-ITEM
            m = re_diag_read.search(line)
            if m:
                ptr = m.group(1)
                size = int(m.group(2))
                free_before = int(m.group(3))
                entries.append({
                    'source': str(p), 'line': lineno, 'tick': last_tick, 'type': 'DIAG-READ-AUDIO-ITEM',
                    'ptr': ptr, 'size': size, 'free_before': free_before, 'raw': line
                })
                continue
            # malloc_usable_size lines
            m = re_malloc_usable.search(line) or re_usable_alt.search(line)
            if m:
                usable = int(m.group(1))
                entries.append({
                    'source': str(p), 'line': lineno, 'tick': last_tick, 'type': 'MALLOC-USABLE',
                    'usable_size': usable, 'raw': line
                })
                continue
            # heap caps free / largest
            m = re_heap_free.search(line)
            if m:
                hf = int(m.group(1))
                entries.append({
                    'source': str(p), 'line': lineno, 'tick': last_tick, 'type': 'HEAP-FREE',
                    'heap_free': hf, 'raw': line
                })
                continue
            m = re_heap_largest.search(line)
            if m:
                hl = int(m.group(1))
                entries.append({
                    'source': str(p), 'line': lineno, 'tick': last_tick, 'type': 'HEAP-LARGEST',
                    'heap_largest': hl, 'raw': line
                })
                continue
            # DIAG-WORKER-ENQ
            m = re_worker_enq2.search(line)
            if m:
                l = int(m.group('len')) if m.group('len') else None
                fb = int(m.group('free_before')) if m.group('free_before') else None
                synth = int(m.group('synth')) if m.group('synth') else None
                entries.append({
                    'source': str(p), 'line': lineno, 'tick': last_tick, 'type': 'DIAG-WORKER-ENQ',
                    'len': l, 'free_before': fb, 'synth': synth, 'raw': line
                })
                continue
            # DIAG-WORKER-RET
            m = re_worker_ret2.search(line)
            if m:
                l = int(m.group('len'))
                fb = int(m.group('free_before'))
                fa = int(m.group('free_after'))
                entries.append({
                    'source': str(p), 'line': lineno, 'tick': last_tick, 'type': 'DIAG-WORKER-RET',
                    'len': l, 'free_before': fb, 'free_after': fa, 'raw': line
                })
                continue
            # Generic DIAG-WORKER lines with ptr/len
            if re_diag_worker.search(line):
                # try to capture ptr and len patterns
                m_ptr = re.search(r"ptr=(0x[0-9a-fA-F]+)", line)
                m_len = re.search(r"len=(\d+)", line)
                m_fb = re.search(r"free_before=(\d+)", line)
                if m_ptr or m_len:
                    entries.append({
                        'source': str(p), 'line': lineno, 'tick': last_tick, 'type': 'DIAG-WORKER-OTHER',
                        'ptr': m_ptr.group(1) if m_ptr else None,
                        'len': int(m_len.group(1)) if m_len else None,
                        'free_before': int(m_fb.group(1)) if m_fb else None,
                        'raw': line
                    })
                    continue
            # TRACE lines
            m = re_trace.search(line)
            if m:
                payload = m.group(1).strip()
                entries.append({
                    'source': str(p), 'line': lineno, 'tick': last_tick, 'type': 'TRACE',
                    'payload': payload, 'raw': line
                })
                continue
            # generic address-only lines: optionally symbolize
            # if ELF+addr2line provided, attempt to map the first address on the line
            if elf_path and addr2line_cmd:
                m = re_addr.search(line)
                if m:
                    addr = m.group(1)
                    # call addr2line -e ELF -f -p ADDR
                    try:
                        proc = subprocess.run([addr2line_cmd, '-e', elf_path, '-f', '-p', addr], capture_output=True, text=True, timeout=1)
                        sym = proc.stdout.strip() if proc.returncode == 0 else ''
                    except Exception:
                        sym = ''
                    entries.append({
                        'source': str(p), 'line': lineno, 'tick': last_tick, 'type': 'ADDR-SYM',
                        'addr': addr, 'symbol': sym, 'raw': line
                    })
                    continue

# write CSV
out_csv = Path('tmp/trace_parsed.csv')
out_json = Path('tmp/trace_parsed.json')
out_csv.parent.mkdir(parents=True, exist_ok=True)

# determine headers
all_keys = set()
for e in entries:
    all_keys.update(e.keys())
headers = ['source','line','tick','type','ptr','size','len','free_before','free_after','synth','payload','raw']

with out_csv.open('w', newline='', encoding='utf-8') as cf:
    writer = csv.DictWriter(cf, fieldnames=headers)
    writer.writeheader()
    for e in entries:
        row = {k: e.get(k, '') for k in headers}
        writer.writerow(row)

with out_json.open('w', encoding='utf-8') as jf:
    json.dump(entries, jf, indent=2)

print(f"Parsed {len(entries)} records from {len(logfiles)} files.")
print(f"CSV: {out_csv.resolve()}")
print(f"JSON: {out_json.resolve()}")
