#!/usr/bin/env python3
import json
import statistics
from collections import Counter

PATH = '/home/phil/work/esp32/esp32_btaudio/esp_bt_audio_source/test/test_app_audio/tmp/trace_parsed.json'

def to_int(x):
    if x is None:
        return None
    try:
        return int(x)
    except Exception:
        try:
            return int(str(x), 0)
        except Exception:
            return None

with open(PATH, 'r') as f:
    records = json.load(f)

print(f'Total records: {len(records)}')

by_type = Counter((r.get('type') or 'UNKNOWN') for r in records)
print('\nCounts by type:')
for k, v in by_type.most_common():
    print(f'  {k}: {v}')

numeric_fields = ['size', 'len', 'free_before', 'free_after']
for field in numeric_fields:
    vals = [to_int(r.get(field)) for r in records]
    vals = [v for v in vals if v is not None]
    if not vals:
        continue
    print(f"\nStats for {field} (n={len(vals)}):")
    print('  min:', min(vals))
    print('  max:', max(vals))
    print('  mean:', round(statistics.mean(vals), 2))
    try:
        print('  median:', statistics.median(vals))
    except Exception:
        pass

ptrs = [r.get('ptr') for r in records if r.get('ptr')]
print('\nPointer stats:')
print('  unique ptrs:', len(set(ptrs)))
ptr_count = Counter(ptrs)
for ptr, count in ptr_count.most_common(10):
    print(f'  {ptr}: {count}')

# quick fragment: records with large free_before/free_after deltas
pairs = [(to_int(r.get('free_before')), to_int(r.get('free_after'))) for r in records if r.get('free_before') is not None and r.get('free_after') is not None]
if pairs:
    diffs = [b - a for a, b in pairs if a is not None and b is not None]
    if diffs:
        print('\nFree delta stats (free_after - free_before):')
        print('  min:', min(diffs))
        print('  max:', max(diffs))
        print('  mean:', round(statistics.mean(diffs), 2))

print('\nDone.')
