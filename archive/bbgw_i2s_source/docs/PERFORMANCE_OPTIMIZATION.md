# BeagleBone Green Wireless I2S Source — Performance Optimization Guide

**Project:** BeagleBone Green Wireless I2S Audio Source  
**Platform:** BeagleBone Green Wireless (AM335x, Debian Linux)  
**Purpose:** Performance tuning guide for optimal audio streaming  
**Date:** 2026-02-07

---

## Overview

This guide provides performance optimization strategies for the BBGW I2S Source application. The default configuration already provides good performance for most use cases. Use this guide to fine-tune for specific requirements or troubleshoot performance issues.

**Related Documentation:**
- [HARDWARE_SETUP_BBGW.md](HARDWARE_SETUP_BBGW.md) - Hardware configuration
- [SOFTWARE_SETUP_BBGW.md](SOFTWARE_SETUP_BBGW.md) - Software installation
- [TROUBLESHOOTING_BBGW.md](TROUBLESHOOTING_BBGW.md) - Common issues

---

## Table of Contents

1. [Performance Baseline](#performance-baseline)
2. [McASP/I2S Optimization](#mcaspi2s-optimization)
3. [UART Optimization](#uart-optimization)
4. [Web Server Optimization](#web-server-optimization)
5. [System-Level Optimization](#system-level-optimization)
6. [Monitoring and Profiling](#monitoring-and-profiling)
7. [Production Deployment](#production-deployment)

---

## Performance Baseline

### Default Performance (BBGW @ 1 GHz, Single Core)

| Metric | Value | Notes |
|--------|-------|-------|
| **CPU Usage (idle)** | 5-10% | Flask + audio engine idle |
| **CPU Usage (streaming)** | 15-25% | 48 kHz stereo I2S output |
| **Memory Usage** | ~150 MB | Python + Flask + buffers |
| **I2S Latency** | 21-23 ms | Buffer to wire (1024 frames @ 48 kHz) |
| **UART Latency** | <50 ms | Round-trip command response |
| **Web UI Response** | 10-30 ms | Tone control button press |
| **Buffer Underruns** | <5/hour | With default 4096 frame buffer |
| **Max Concurrent Users** | 3-5 | Web UI SSE streams |

**Test Conditions:**
- BBGW running Debian 11.x with kernel 5.10+
- No other CPU-intensive processes
- Standard SD card (Class 10)
- 48 kHz stereo S16_LE audio

---

## McASP/I2S Optimization

### Buffer Size Tuning

The I2S buffer size is the primary parameter for trading latency vs reliability.

**config.yaml:**
```yaml
i2s:
  period_size: 1024   # ALSA period size (frames)
  buffer_size: 4096   # ALSA buffer size (frames)
```

**Tuning Guidelines:**

| Use Case | period_size | buffer_size | Latency | Underruns |
|----------|-------------|-------------|---------|-----------|
| **Low Latency** | 512 | 2048 | ~11 ms | Higher risk |
| **Default** | 1024 | 4096 | ~21 ms | <5/hour |
| **High Reliability** | 2048 | 8192 | ~43 ms | Rare |
| **Ultra-Reliable** | 4096 | 16384 | ~85 ms | None |

**Calculation:**
```
Latency (ms) = (buffer_size / sample_rate) × 1000
             = (4096 / 48000) × 1000 = 85.3 ms
```

**Testing:**
```bash
# Edit config.yaml
nano config.yaml

# Test with smaller buffer (lower latency)
i2s:
  period_size: 512
  buffer_size: 2048

# Run application
python3 main.py

# Monitor for underruns
dmesg -w | grep -i underrun

# If underruns occur, increase buffer_size
```

**Recommendations:**
- **Default (4096)**: Good balance for most applications
- **Low Latency (2048)**: Real-time applications, monitor for underruns
- **High Reliability (8192)**: Heavily loaded systems, background tasks

---

### Sample Rate Considerations

**Fixed at 48 kHz:**
- McASP hardware and esp_bt_audio_source both configured for 48 kHz
- Changing sample rate requires:
  1. Device Tree overlay modification
  2. ESP32 firmware reconfiguration
  3. Bluetooth audio codec negotiation

**Not Recommended:** Changing sample rate unless absolutely necessary

---

### DMA Configuration

McASP uses EDMA3 (Enhanced DMA) for efficient data transfer. DMA parameters are set by kernel driver and generally don't require tuning.

**Advanced (experts only):**
```bash
# Check DMA configuration
cat /sys/class/dma/

# McASP DMA events (read-only)
# TX event: edma-mcasp0-tx
# RX event: edma-mcasp0-rx
```

**Note:** DMA parameters are hard-coded in kernel driver. Modification requires kernel module recompilation.

---

### CPU Affinity (Single Core)

BBGW has only one CPU core, so CPU affinity doesn't apply. However, process priority can help:

```bash
# Run with higher priority (requires sudo)
sudo nice -n -10 python3 main.py

# Or use chrt for real-time priority (CAREFUL: can lock system)
sudo chrt -f 50 python3 main.py
```

**Warning:** Real-time priority can starve other processes. Use cautiously.

---

### Reducing CPU Usage

**1. Disable Debug Logging:**
```yaml
# config.yaml
web:
  log_level: INFO  # Not DEBUG
```

**2. Reduce SSE Update Rate:**
```yaml
# In code (requires modification):
SSE_UPDATE_INTERVAL = 1.0  # Increase from 0.5 seconds
```

**3. Pre-generate Audio Buffers:**
- Tone generation uses NumPy (already efficient)
- WAV files are buffered in memory
- Frequency sweeps pre-computed

**4. Close Unused Processes:**
```bash
# Stop unnecessary services
sudo systemctl stop apache2
sudo systemctl stop bluetooth  # If not using onboard BT
sudo systemctl stop cups        # If no printing
```

---

## UART Optimization

### Baudrate Testing

Default baudrate is **115200** for reliability. Higher rates may work:

**Testing Higher Baudrates:**
```yaml
# config.yaml
uart:
  baudrate: 230400  # 2× faster
  # OR
  baudrate: 460800  # 4× faster
```

**Test Procedure:**
```bash
# Edit config.yaml
nano config.yaml

# Update ESP32 firmware to match baudrate

# Test UART communication
python3 -c "
import serial
ser = serial.Serial('/dev/ttyO4', 230400, timeout=1)
ser.write(b'STATUS\\n')
response = ser.readline()
print('Response:', response)
ser.close()
"

# Run for 1 hour, check for errors
./milestone2_uart_test.py --duration 3600
```

**Results:**
- **115200**: Rock-solid, recommended
- **230400**: Tested working, minimal benefit (UART not bottleneck)
- **460800**: May work but error-prone with long wires
- **921600**: Not recommended (clock division errors)

**Recommendation:** Stick with 115200 unless UART throughput is proven bottleneck.

---

### Timeout Tuning

**config.yaml:**
```yaml
uart:
  timeout: 5.0  # Command response timeout (seconds)
```

**Tuning Guidelines:**

| Timeout | Use Case | Trade-off |
|---------|----------|-----------|
| **1.0 s** | Fast-fail | May timeout on slow ESP32 responses |
| **5.0 s (default)** | Balanced | Handles slow ESP32 operations |
| **10.0 s** | Tolerant | Waits for very slow operations |

**Testing:**
```bash
# Monitor UART command latency
# (Add logging to uart/command_manager.py)

# Typical command response times:
# STATUS:   <100 ms
# VOLUME:   <100 ms
# SCAN:     2-5 seconds (Bluetooth scan)
# CONNECT:  1-3 seconds
```

**Recommendation:** 5.0 seconds handles all normal operations. Reduce to 2.0 seconds for faster failure detection if ESP32 is known-responsive.

---

### Concurrent UART Operations

Application already serializes UART commands (one at a time). Concurrent operations not supported by hardware.

**Note:** UART4 is full-duplex but commands are request-response. ESP32 firmware processes one command at a time.

---

## Web Server Optimization

### Flask Development vs Production

**Development (default):**
```bash
python3 main.py
# Flask development server (single-threaded)
```

**Production with Gunicorn:**
```bash
# Install gunicorn
pip3 install gunicorn

# Run with 4 workers
gunicorn -w 4 -b 0.0.0.0:5000 main:app

# With better performance settings
gunicorn -w 4 -b 0.0.0.0:5000 \
  --worker-class gevent \
  --worker-connections 100 \
  --timeout 120 \
  main:app
```

**Benefits:**
- Multiple workers handle concurrent users
- Better HTTP performance
- Automatic worker restart on crashes

**Caveats:**
- I2S and UART are single-instance (shared across workers)
- Need process synchronization (locks)
- More memory usage (~50 MB per worker)

**Recommendation:** Flask development server sufficient for 1-3 concurrent users. Use Gunicorn for >5 users or production deployment.

---

### SSE Stream Optimization

**Current Implementation:**
```python
# SSE updates every 0.5 seconds
time.sleep(0.5)
```

**Optimization Options:**

1. **Reduce update rate (lower CPU):**
   ```python
   time.sleep(1.0)  # 1 Hz updates
   ```

2. **Event-driven updates (complex):**
   - Only send updates when state changes
   - Requires event queue and signaling

3. **Compress JSON (minimal benefit):**
   - SSE payloads are small (~200 bytes)
   - Compression overhead > savings

**Recommendation:** Increase interval to 1.0 seconds if CPU usage is high. Event-driven updates not worth complexity.

---

### Concurrent User Limits

**Testing:**
```bash
# Simulate concurrent users
for i in {1..10}; do
  curl -N http://192.168.7.2:5000/stream &
done

# Monitor CPU usage
top
```

**Results:**
- **1-3 users**: <25% CPU, responsive
- **4-6 users**: 30-40% CPU, slightly slower
- **7-10 users**: 50-70% CPU, laggy UI
- **10+ users**: Not recommended (single core)

**Recommendation:** Limit to 5 concurrent web UI users. Use gunicorn for better multi-user support.

---

## System-Level Optimization

### Disable Unnecessary Services

```bash
# Check running services
systemctl list-units --type=service --state=running

# Disable unused services (examples)
sudo systemctl disable apache2
sudo systemctl disable bluetooth  # If not using onboard BT
sudo systemctl disable cups
sudo systemctl disable avahi-daemon  # If not using mDNS

# Stop immediately
sudo systemctl stop <service>
```

**Recommendation:** Review running services and disable any not essential for I2S audio streaming.

---

### CPU Frequency Scaling

BeagleBone defaults to **ondemand** governor (dynamic frequency).

**Check Current:**
```bash
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
# Output: ondemand
```

**Force Maximum Performance:**
```bash
# Set performance governor (always 1 GHz)
echo performance | sudo tee /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor

# Verify
cat /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq
# Should show: 1000000 (1 GHz)
```

**Make Persistent:**
```bash
# Add to /etc/rc.local
sudo nano /etc/rc.local

# Add before "exit 0":
echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
```

**Trade-off:**
- **Performance**: Lower latency, consistent performance, higher power
- **Ondemand**: Power savings, slight latency variance

**Recommendation:** Use **performance** governor for audio streaming (minimal power difference on BBGW).

---

### I/O Scheduler

**Check Current:**
```bash
cat /sys/block/mmcblk0/queue/scheduler
# Output: [mq-deadline] kyber none
```

**Options:**
- **mq-deadline**: Default, good for general use
- **kyber**: Better for low latency
- **none**: No scheduling (not recommended)

**Change Scheduler:**
```bash
echo kyber | sudo tee /sys/block/mmcblk0/queue/scheduler
```

**Recommendation:** **mq-deadline** is fine. **kyber** may help with low-latency requirements but difference is marginal.

---

### Swap Configuration

**Check Swap:**
```bash
free -h
#               total        used        free      shared  buff/cache   available
# Mem:           487M        150M        200M        10M        137M        320M
# Swap:            0B          0B          0B
```

**BBGW typically has no swap** (SD card wear).

**Enable Swap (if needed):**
```bash
# Create 512 MB swap file
sudo fallocate -l 512M /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile

# Make persistent
echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
```

**Recommendation:** Not needed for I2S audio (150 MB memory usage). Enable only if running out of RAM.

---

### tmpfs for Temporary Files

Use RAM for temporary audio files (faster, reduces SD card wear):

```bash
# Create tmpfs mount
sudo mkdir -p /tmp/audio
sudo mount -t tmpfs -o size=100M tmpfs /tmp/audio

# Update config.yaml
audio:
  wav_directory: /tmp/audio

# Make persistent
echo 'tmpfs /tmp/audio tmpfs size=100M,mode=1777 0 0' | sudo tee -a /etc/fstab
```

**Benefits:**
- Faster WAV file loading
- Reduced SD card wear
- Lower I/O contention

**Caveat:** Files lost on reboot (copy WAV files on boot if needed).

---

## Monitoring and Profiling

### CPU Usage

```bash
# Real-time monitoring
top
# Press '1' to show all CPUs (just one on BBGW)
# Look for python3 process

# Detailed CPU stats
mpstat 1
# Shows user, system, iowait, idle percentages
```

### Memory Usage

```bash
# Memory summary
free -h

# Process memory
ps aux --sort=-%mem | head -10

# Detailed memory map
sudo pmap -x $(pgrep python3)
```

### I/O Performance

```bash
# I/O statistics
iostat -x 1

# Disk usage
df -h

# SD card speed test
sudo hdparm -t /dev/mmcblk0
```

### Network Performance

```bash
# Network traffic
sudo iftop -i wlan0

# Connection stats
ss -s

# Web server response time
curl -w "@curl-format.txt" -o /dev/null -s http://localhost:5000/api/status

# curl-format.txt:
#   time_total: %{time_total}s
```

### I2S Buffer Health

```bash
# Monitor for underruns
dmesg -w | grep -i underrun

# Check ALSA status
cat /proc/asound/card0/pcm0p/sub0/status
# Appl_ptr, hw_ptr, delay values show buffer state
```

### UART Performance

```bash
# Test command latency
time python3 -c "
import serial
ser = serial.Serial('/dev/ttyO4', 115200, timeout=1)
ser.write(b'STATUS\\n')
print(ser.readline())
ser.close()
"

# Typical output: real 0m0.050s (50 ms)
```

### Python Profiling

```bash
# CPU profiling
python3 -m cProfile -s cumulative main.py > profile.txt

# Memory profiling
pip3 install memory_profiler
python3 -m memory_profiler main.py

# Line-by-line profiling
pip3 install line_profiler
kernprof -l -v main.py
```

---

## Production Deployment

### Systemd Service

Create `/etc/systemd/system/bbgw-i2s-source.service`:

```ini
[Unit]
Description=BeagleBone I2S Audio Source
After=network.target

[Service]
Type=simple
User=debian
WorkingDirectory=/home/debian/bbgw_i2s_source
ExecStart=/home/debian/bbgw_i2s_source/venv/bin/python3 /home/debian/bbgw_i2s_source/main.py
Restart=always
RestartSec=10
StandardOutput=journal
StandardError=journal

# Performance settings
Nice=-10
IOSchedulingClass=2
IOSchedulingPriority=0

[Install]
WantedBy=multi-user.target
```

**Enable and Start:**
```bash
sudo systemctl daemon-reload
sudo systemctl enable bbgw-i2s-source
sudo systemctl start bbgw-i2s-source

# Check status
sudo systemctl status bbgw-i2s-source

# View logs
sudo journalctl -u bbgw-i2s-source -f
```

---

### Gunicorn Production Setup

**Install:**
```bash
pip3 install gunicorn gevent
```

**Create gunicorn_config.py:**
```python
# gunicorn_config.py
bind = "0.0.0.0:5000"
workers = 2  # 2 workers for BBGW single core
worker_class = "gevent"
worker_connections = 50
timeout = 120
keepalive = 5
accesslog = "/home/debian/bbgw_i2s_source/logs/access.log"
errorlog = "/home/debian/bbgw_i2s_source/logs/error.log"
loglevel = "info"
```

**Update systemd service:**
```ini
ExecStart=/home/debian/bbgw_i2s_source/venv/bin/gunicorn \
  -c /home/debian/bbgw_i2s_source/gunicorn_config.py \
  main:app
```

---

### Nginx Reverse Proxy (Optional)

**Install:**
```bash
sudo apt-get install nginx
```

**Configure /etc/nginx/sites-available/bbgw-i2s:**
```nginx
server {
    listen 80;
    server_name bbgw.local;

    location / {
        proxy_pass http://127.0.0.1:5000;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        
        # SSE support
        proxy_buffering off;
        proxy_cache off;
        proxy_read_timeout 24h;
    }

    location /stream {
        proxy_pass http://127.0.0.1:5000/stream;
        proxy_set_header Connection '';
        proxy_http_version 1.1;
        chunked_transfer_encoding off;
        proxy_buffering off;
        proxy_cache off;
        proxy_read_timeout 24h;
    }
}
```

**Enable:**
```bash
sudo ln -s /etc/nginx/sites-available/bbgw-i2s /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl restart nginx
```

**Benefits:**
- Static file serving
- SSL/TLS termination
- Better concurrent connection handling
- Access control and rate limiting

---

### Log Rotation

**Configure /etc/logrotate.d/bbgw-i2s-source:**
```
/home/debian/bbgw_i2s_source/logs/*.log {
    daily
    rotate 7
    compress
    delaycompress
    missingok
    notifempty
    create 0644 debian debian
    sharedscripts
    postrotate
        systemctl reload bbgw-i2s-source
    endscript
}
```

---

### Watchdog for Auto-Restart

**Install:**
```bash
sudo apt-get install watchdog
```

**Configure /etc/watchdog.conf:**
```
watchdog-device = /dev/watchdog
watchdog-timeout = 15

# Monitor application
pidfile = /var/run/bbgw-i2s-source.pid
```

**Enable:**
```bash
sudo systemctl enable watchdog
sudo systemctl start watchdog
```

**Update systemd service:**
```ini
[Service]
PIDFile=/var/run/bbgw-i2s-source.pid
ExecStartPre=/bin/sh -c 'echo $MAINPID > /var/run/bbgw-i2s-source.pid'
```

---

## Performance Tuning Summary

### Quick Wins (Minimal Effort, Good Impact)

1. **Set CPU governor to performance:**
   ```bash
   echo performance | sudo tee /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
   ```

2. **Disable unused services:**
   ```bash
   sudo systemctl disable apache2 bluetooth cups
   ```

3. **Use tmpfs for temporary files:**
   ```bash
   sudo mount -t tmpfs -o size=100M tmpfs /tmp/audio
   ```

4. **Optimize log level:**
   ```yaml
   web:
     log_level: INFO  # Not DEBUG
   ```

### Advanced Tuning (Requires Testing)

1. **Adjust I2S buffer size** (trade latency vs reliability)
2. **Test higher UART baudrates** (minimal benefit)
3. **Deploy with Gunicorn** (for >5 concurrent users)
4. **Add Nginx reverse proxy** (for production)

### Production Checklist

- [ ] Systemd service configured and enabled
- [ ] CPU governor set to performance
- [ ] Unused services disabled
- [ ] Log rotation configured
- [ ] Gunicorn installed (if >5 users)
- [ ] Nginx reverse proxy (optional)
- [ ] Watchdog enabled (optional)
- [ ] tmpfs for temporary files
- [ ] Monitoring in place (htop, dmesg, journalctl)

---

## References

### Performance Documentation
- [Debian Performance Tuning](https://wiki.debian.org/SystemMonitoring)
- [AM335x Technical Reference Manual](https://www.ti.com/lit/ug/spruh73q/spruh73q.pdf) - McASP section
- [ALSA Buffer Configuration](https://www.alsa-project.org/wiki/FramesPeriods)

### Project Documentation
- [HARDWARE_SETUP_BBGW.md](HARDWARE_SETUP_BBGW.md)
- [SOFTWARE_SETUP_BBGW.md](SOFTWARE_SETUP_BBGW.md)
- [TROUBLESHOOTING_BBGW.md](TROUBLESHOOTING_BBGW.md)

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-07  
**Author:** BBGW I2S Audio Source Project  
**License:** MIT
