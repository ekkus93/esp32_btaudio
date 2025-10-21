#!/usr/bin/env python3
"""
Generate histogram and CDF PNGs from parse_seq_ts CSV output.

Usage:
  python3 tools/plot_jitter.py --inputs tmp/dump_event_stress_output_1k_micro.csv tmp/dump_event_stress_output_10k_micro.csv tmp/dump_event_stress_output_50k_micro.csv --out-dir tmp/

Produces per-file histogram and CDF PNGs and combined CDF + histogram.
"""
import argparse
import os
import sys
import pandas as pd
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def read_ts_us(path):
    df = pd.read_csv(path)
    if 'ts_us' not in df.columns:
        raise ValueError(f"CSV {path} missing ts_us column")
    # delta_ts_us column may be empty for first row
    if 'delta_ts_us' in df.columns:
        deltas = df['delta_ts_us'].dropna().astype(float).to_numpy()
    else:
        # compute from ts_us
        deltas = np.diff(df['ts_us'].astype(np.int64).to_numpy()).astype(float)
    return deltas


def plot_hist(deltas, outpath, title=None, bins=100, range_us=(0, 5000), logy=False, dpi=150, fmt='png'):
    plt.figure(figsize=(8,4))
    plt.hist(deltas, bins=bins, range=range_us, color='#4C72B0', edgecolor='k')
    if logy:
        plt.yscale('log')
    plt.xlabel('delta ts (us)')
    plt.ylabel('count')
    if title:
        plt.title(title)
    plt.tight_layout()
    plt.savefig(outpath, dpi=dpi, format=fmt)
    plt.close()


def plot_cdf(deltas, outpath, title=None, range_us=(0,5000), logx=False, dpi=150, fmt='png'):
    vals = np.sort(deltas)
    cdf = np.arange(1, len(vals)+1) / len(vals)
    plt.figure(figsize=(8,4))
    plt.plot(vals, cdf, color='#DD8452')
    if logx:
        plt.xscale('log')
    plt.xlim(range_us)
    plt.xlabel('delta ts (us)')
    plt.ylabel('CDF')
    if title:
        plt.title(title)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(outpath, dpi=dpi, format=fmt)
    plt.close()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--inputs', nargs='+', required=True, help='CSV files from parse_seq_ts.py')
    p.add_argument('--out-dir', default='tmp', help='Output directory for PNGs')
    p.add_argument('--bins', type=int, default=100, help='Histogram bins')
    p.add_argument('--range', type=int, nargs=2, default=(0,5000), help='Histogram/CDF x-axis range in us')
    p.add_argument('--logx', action='store_true', help='Use log scale for x-axis on CDFs')
    p.add_argument('--logy', action='store_true', help='Use log scale for y-axis on histograms')
    p.add_argument('--tail-start', type=int, default=1500, help='Start (us) for tail-zoom plots')
    p.add_argument('--dpi', type=int, default=150, help='DPI for output PNGs')
    p.add_argument('--format', choices=['png','svg'], default='png', help='Output image format')
    args = p.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    all_deltas = []
    labels = []
    for path in args.inputs:
        if not os.path.exists(path):
            print(f"Skipping missing file: {path}", file=sys.stderr)
            continue
        deltas = read_ts_us(path)
        base = os.path.basename(path)
        name = os.path.splitext(base)[0]
        labels.append(name)
        all_deltas.append((name, deltas))
        # per-file plots
        hist_out = os.path.join(args.out_dir, f"{name}_hist.{args.format}")
        cdf_out = os.path.join(args.out_dir, f"{name}_cdf.{args.format}")
        try:
            plot_hist(deltas, hist_out, title=f"Histogram {name}", bins=args.bins, range_us=tuple(args.range), logy=args.logy, dpi=args.dpi, fmt=args.format)
            plot_cdf(deltas, cdf_out, title=f"CDF {name}", range_us=tuple(args.range), logx=args.logx, dpi=args.dpi, fmt=args.format)
            print(f"Wrote: {hist_out}, {cdf_out}")
        except Exception as e:
            print(f"Failed plotting {name}: {e}", file=sys.stderr)

    # combined CDF
    plt.figure(figsize=(8,4))
    for name, deltas in all_deltas:
        vals = np.sort(deltas)
        cdf = np.arange(1, len(vals)+1) / len(vals)
        plt.plot(vals, cdf, label=name)
    plt.xlim(tuple(args.range))
    plt.xlabel('delta ts (us)')
    plt.ylabel('CDF')
    plt.legend()
    plt.grid(True, alpha=0.3)
    out_comb_cdf = os.path.join(args.out_dir, f"combined_cdf.{args.format}")
    plt.tight_layout()
    plt.savefig(out_comb_cdf, dpi=args.dpi, format=args.format)
    plt.close()
    print(f"Wrote: {out_comb_cdf}")

    # combined histogram (stacked)
    plt.figure(figsize=(8,4))
    bins = np.linspace(args.range[0], args.range[1], args.bins+1)
    for name, deltas in all_deltas:
        plt.hist(deltas, bins=bins, alpha=0.5, label=name)
    plt.xlabel('delta ts (us)')
    plt.ylabel('count')
    plt.legend()
    out_comb_hist = os.path.join(args.out_dir, f"combined_hist.{args.format}")
    plt.tight_layout()
    plt.savefig(out_comb_hist, dpi=args.dpi, format=args.format)
    plt.close()
    print(f"Wrote: {out_comb_hist}")

    # Tail zoom plots (for visualizing rare long-tail delays)
    for name, deltas in all_deltas:
        tail = deltas[deltas >= args.tail_start]
        if len(tail) == 0:
            continue
        tail_hist = os.path.join(args.out_dir, f"{name}_tail_hist.{args.format}")
        tail_cdf = os.path.join(args.out_dir, f"{name}_tail_cdf.{args.format}")
        try:
            plot_hist(tail, tail_hist, title=f"Tail Histogram {name} >= {args.tail_start}us", bins=args.bins, range_us=(args.tail_start, max(tail.max(), args.tail_start+1)), logy=args.logy, dpi=args.dpi, fmt=args.format)
            plot_cdf(tail, tail_cdf, title=f"Tail CDF {name} >= {args.tail_start}us", range_us=(args.tail_start, max(tail.max(), args.tail_start+1)), logx=args.logx, dpi=args.dpi, fmt=args.format)
            print(f"Wrote: {tail_hist}, {tail_cdf}")
        except Exception as e:
            print(f"Failed tail plotting {name}: {e}", file=sys.stderr)


if __name__ == '__main__':
    main()
