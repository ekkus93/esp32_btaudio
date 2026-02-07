#!/usr/bin/env python3
"""
Standalone resource monitoring utility for BeagleBone Green Wireless I2S Source.

This script monitors CPU and memory usage during audio playback operations.
Useful for performance profiling and validation of NFRs.

Usage:
    python monitor_resources.py --duration=300
    python monitor_resources.py --duration=300 --interval=5 --output=results.csv

Requirements:
    - psutil
    - requests (if monitoring remote API)
"""

import argparse
import time
import sys
import psutil
import csv
from datetime import datetime


def find_process_by_name(name_pattern):
    """Find process matching the given name pattern."""
    for proc in psutil.process_iter(['pid', 'name', 'cmdline']):
        try:
            # Check if process name or command line contains pattern
            if name_pattern.lower() in proc.info['name'].lower():
                return proc
            if proc.info['cmdline']:
                cmdline = ' '.join(proc.info['cmdline']).lower()
                if name_pattern.lower() in cmdline:
                    return proc
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return None


def monitor_resources(duration_sec, interval_sec=1, process_name=None, output_file=None):
    """
    Monitor CPU and memory usage over a specified duration.
    
    Args:
        duration_sec: Total monitoring duration in seconds
        interval_sec: Sampling interval in seconds
        process_name: Optional process name to monitor (default: current process)
        output_file: Optional CSV file to write results
    """
    if process_name:
        print(f"Finding process: {process_name}...")
        process = find_process_by_name(process_name)
        if not process:
            print(f"Error: Process '{process_name}' not found")
            sys.exit(1)
        print(f"Monitoring process: PID {process.pid} - {process.name()}")
    else:
        process = psutil.Process()
        print(f"Monitoring current process: PID {process.pid}")
    
    print(f"\nMonitoring for {duration_sec} seconds (interval: {interval_sec}s)")
    print(f"Started at: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("\n{:>6s} {:>10s} {:>10s} {:>10s} {:>8s}".format(
        "Time", "CPU%", "RSS(MB)", "VMS(MB)", "Threads"
    ))
    print("-" * 55)
    
    # Data collection
    samples = []
    start_time = time.time()
    elapsed = 0
    
    csv_writer = None
    csv_file = None
    
    if output_file:
        csv_file = open(output_file, 'w', newline='')
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(['Timestamp', 'Elapsed_Sec', 'CPU_Percent', 'RSS_MB', 'VMS_MB', 'Threads'])
    
    try:
        while elapsed < duration_sec:
            try:
                # Sample CPU and memory
                cpu_percent = process.cpu_percent(interval=0.1)
                mem_info = process.memory_info()
                num_threads = process.num_threads()
                
                rss_mb = mem_info.rss / (1024 * 1024)
                vms_mb = mem_info.vms / (1024 * 1024)
                
                sample = {
                    'timestamp': datetime.now(),
                    'elapsed': elapsed,
                    'cpu_percent': cpu_percent,
                    'rss_mb': rss_mb,
                    'vms_mb': vms_mb,
                    'threads': num_threads
                }
                samples.append(sample)
                
                # Print sample
                print("{:6.0f}s {:9.1f}% {:9.1f} MB {:9.1f} MB {:7d}".format(
                    elapsed, cpu_percent, rss_mb, vms_mb, num_threads
                ))
                
                # Write to CSV
                if csv_writer:
                    csv_writer.writerow([
                        sample['timestamp'].strftime('%Y-%m-%d %H:%M:%S'),
                        elapsed,
                        f"{cpu_percent:.1f}",
                        f"{rss_mb:.1f}",
                        f"{vms_mb:.1f}",
                        num_threads
                    ])
                
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                print(f"\nError: Process terminated or access denied")
                break
            
            # Wait for next sample
            time.sleep(interval_sec)
            elapsed = time.time() - start_time
    
    except KeyboardInterrupt:
        print("\n\nMonitoring interrupted by user")
    
    finally:
        if csv_file:
            csv_file.close()
            print(f"\nResults written to: {output_file}")
    
    # Print summary statistics
    if samples:
        print("\n" + "=" * 55)
        print("SUMMARY STATISTICS")
        print("=" * 55)
        
        cpu_values = [s['cpu_percent'] for s in samples]
        rss_values = [s['rss_mb'] for s in samples]
        
        print(f"\nCPU Usage:")
        print(f"  Average: {sum(cpu_values)/len(cpu_values):.1f}%")
        print(f"  Minimum: {min(cpu_values):.1f}%")
        print(f"  Maximum: {max(cpu_values):.1f}%")
        
        print(f"\nMemory (RSS):")
        print(f"  Average: {sum(rss_values)/len(rss_values):.1f} MB")
        print(f"  Minimum: {min(rss_values):.1f} MB")
        print(f"  Maximum: {max(rss_values):.1f} MB")
        print(f"  Range: {max(rss_values) - min(rss_values):.1f} MB")
        
        # Calculate memory growth rate
        if len(samples) > 1:
            n = len(samples)
            elapsed_times = [s['elapsed'] for s in samples]
            
            sum_x = sum(elapsed_times)
            sum_y = sum(rss_values)
            sum_xy = sum(t * m for t, m in zip(elapsed_times, rss_values))
            sum_x2 = sum(t * t for t in elapsed_times)
            
            if (n * sum_x2 - sum_x * sum_x) != 0:
                slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x)
                growth_mb_per_sec = slope
                growth_mb_per_min = slope * 60
                
                print(f"  Growth rate: {growth_mb_per_min:.3f} MB/minute")
                
                if abs(growth_mb_per_min) < 0.1:
                    print(f"  ✓ Memory stable (growth < 0.1 MB/min)")
                elif growth_mb_per_min < 1.0:
                    print(f"  ⚠ Slight memory growth (target: <1 MB/min)")
                else:
                    print(f"  ✗ Memory leak detected (growth > 1 MB/min)")
        
        print(f"\nThreads: {samples[-1]['threads']}")
        print(f"Samples collected: {len(samples)}")
        print(f"Actual duration: {elapsed:.1f} seconds")


def main():
    parser = argparse.ArgumentParser(
        description='Monitor CPU and memory usage of BeagleBone Green Wireless I2S Source',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Monitor for 5 minutes (300 seconds)
  python monitor_resources.py --duration=300
  
  # Monitor with 5-second intervals and save to CSV
  python monitor_resources.py --duration=300 --interval=5 --output=perf.csv
  
  # Monitor specific process
  python monitor_resources.py --duration=60 --process=main.py
        """
    )
    
    parser.add_argument(
        '--duration',
        type=int,
        required=True,
        help='Monitoring duration in seconds'
    )
    
    parser.add_argument(
        '--interval',
        type=int,
        default=1,
        help='Sampling interval in seconds (default: 1)'
    )
    
    parser.add_argument(
        '--process',
        type=str,
        default=None,
        help='Process name pattern to monitor (default: current process)'
    )
    
    parser.add_argument(
        '--output',
        type=str,
        default=None,
        help='Output CSV file path (optional)'
    )
    
    args = parser.parse_args()
    
    # Validate arguments
    if args.duration < 1:
        print("Error: Duration must be at least 1 second")
        sys.exit(1)
    
    if args.interval < 1:
        print("Error: Interval must be at least 1 second")
        sys.exit(1)
    
    if args.interval > args.duration:
        print("Error: Interval cannot be larger than duration")
        sys.exit(1)
    
    # Run monitoring
    monitor_resources(
        duration_sec=args.duration,
        interval_sec=args.interval,
        process_name=args.process,
        output_file=args.output
    )


if __name__ == '__main__':
    main()
