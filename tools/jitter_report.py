#!/usr/bin/env python3
import csv
import sys
from statistics import mean, median

if len(sys.argv) < 2:
    print("Usage: jitter_report.py <csv-file>")
    sys.exit(1)

CSV = sys.argv[1]

us_deltas = []
with open(CSV, 'r') as f:
    reader = csv.DictReader(f)
    prev = None
    for row in reader:
        try:
            seq = int(row['seq'])
            ts = int(row['ts_us'])
        except Exception:
            continue
        if prev is not None:
            us_deltas.append(ts - prev)
        prev = ts

if not us_deltas:
    print('No deltas found in', CSV)
    sys.exit(1)

def percentile(data, p):
    if not data: return None
    k = (len(data)-1) * (p/100.0)
    f = int(k)
    c = min(f+1, len(data)-1)
    if f == c:
        return sorted(data)[int(k)]
    d0 = sorted(data)[f] * (c - k)
    d1 = sorted(data)[c] * (k - f)
    return d0 + d1

data = sorted(us_deltas)
count = len(data)
print(f"Samples: {count}")
print(f"min={data[0]}us, max={data[-1]}us, mean={mean(data):.2f}us, median={median(data)}us")
for p in (50, 90, 95, 99, 99.9):
    val = percentile(data, p)
    if val is not None:
        print(f"p{p}: {val}us")

# Histogram buckets (us): 0-499, 500-749, 750-899, 900-999, 1000-1249, 1250-1499, 1500-1999, 2000-4999, >=5000
buckets = [0]*9
for v in data:
    if v < 500: buckets[0]+=1
    elif v < 750: buckets[1]+=1
    elif v < 900: buckets[2]+=1
    elif v < 1000: buckets[3]+=1
    elif v < 1250: buckets[4]+=1
    elif v < 1500: buckets[5]+=1
    elif v < 2000: buckets[6]+=1
    elif v < 5000: buckets[7]+=1
    else: buckets[8]+=1

labels = ['<500us','500-749us','750-899us','900-999us','1000-1249us','1250-1499us','1500-1999us','2000-4999us','>=5000us']
for lbl, cnt in zip(labels, buckets):
    print(f"{lbl}: {cnt} ({cnt/count*100:.2f}%)")
#!/usr/bin/env python3
import csv
import sys
from statistics import mean, median
from collections import Counter

def load_deltas(csv_path):
    deltas = []
    with open(csv_path, newline='') as f:
        reader = csv.DictReader(f)
        for row in reader:
            dt = row.get('delta_ts_ms','')
            if dt is None or dt == '':
                continue
            try:
                deltas.append(int(dt))
            except ValueError:
                continue
    return deltas

def percentile(sorted_list, p):
    if not sorted_list:
        return None
    k = (len(sorted_list)-1) * (p/100.0)
    f = int(k)
    c = f + 1
    if c >= len(sorted_list):
        return sorted_list[f]
    d0 = sorted_list[f] * (c - k)
    d1 = sorted_list[c] * (k - f)
    return d0 + d1

def bucket_hist(deltas, max_bucket=10):
    # buckets: 0,1,2,...,max_bucket-1, >=max_bucket
    buckets = {i:0 for i in range(max_bucket)}
    buckets[f">={max_bucket}"] = 0
    for d in deltas:
        if d < 0:
            continue
        if d < max_bucket:
            buckets[d] += 1
        else:
            buckets[f">={max_bucket}"] += 1
    return buckets

def report(csv_path, out_path=None):
    deltas = load_deltas(csv_path)
    n = len(deltas)
    if n == 0:
        print(f"No deltas found in {csv_path}")
        return
    s = sorted(deltas)
    stats = {
        'count': n,
        'min': s[0],
        'max': s[-1],
        'mean': mean(s),
        'median': median(s),
        'p90': percentile(s, 90),
        'p95': percentile(s, 95),
        'p99': percentile(s, 99),
    }
    buckets = bucket_hist(s, max_bucket=10)

    lines = []
    lines.append(f"Jitter report for: {csv_path}")
    lines.append(f"Samples: {n}")
    lines.append(f"min={stats['min']} ms, max={stats['max']} ms, mean={stats['mean']:.3f} ms, median={stats['median']} ms")
    lines.append(f"p90={stats['p90']} ms, p95={stats['p95']} ms, p99={stats['p99']} ms")
    lines.append("")
    lines.append("Histogram (ms):")
    total = float(n)
    for k in range(0,10):
        cnt = buckets[k]
        pct = cnt/total*100
        lines.append(f"  {k:2d} ms : {cnt:6d} ({pct:5.2f}%)")
    cnt = buckets[">=10"]
    lines.append(f" >=10 ms : {cnt:6d} ({cnt/total*100:5.2f}%)")

    report_text = "\n".join(lines)
    if out_path:
        with open(out_path, 'w') as outf:
            outf.write(report_text)
    print(report_text)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Usage: jitter_report.py <csv_file> [out_report.txt]')
        sys.exit(2)
    csv_path = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else None
    report(csv_path, out_path)
