#!/usr/bin/env python3
import sys
import re

if len(sys.argv) < 2:
    print('Usage: parse_map_bss.py <map-file>')
    sys.exit(2)

mapfile = sys.argv[1]

# compact bss list
bss_entries = []
# Pattern: .bss.symbol_name\n <addr> <size> <owner>
# The map lines sometimes show the symbol on one line and the address/size
# on the next, for example:
#   .bss.s_audio_stats
#   0x00000000 0x1c esp-idf/main/libmain.a(audio_processor.c.obj)

with open(mapfile, 'r', encoding='utf-8') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    m = re.match(r"\s*\.bss(?:\.(\S+))?\s*$", line)
    if m:
        sym = m.group(1) or '<anonymous>'
        # next non-empty line should contain addr size owner
        j = i+1
        while j < len(lines) and lines[j].strip() == '':
            j += 1
        if j >= len(lines):
            continue
        parts = lines[j].strip().split()
        if len(parts) >= 3:
            addr = parts[0]
            size_hex = parts[1]
            owner = ' '.join(parts[2:])
            try:
                size = int(size_hex, 16)
            except ValueError:
                size = 0
            bss_entries.append((size, sym, owner.strip()))

# sort descending by size
bss_entries.sort(reverse=True)

print(f"Found {len(bss_entries)} .bss entries\n")
print(f"{'size':>8}  {'symbol':30}  owner")
print('-'*80)
for size, sym, owner in bss_entries[:200]:
    print(f"{size:8}  {sym:30}  {owner}")

# also print total bss
total = sum(e[0] for e in bss_entries)
print('\nTotal of listed .bss sizes: {}'.format(total))
