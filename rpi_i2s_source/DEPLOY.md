# Deployment Guide

Complete deployment checklist and release preparation for the Raspberry Pi I2S Audio Test Jig.

---

## Table of Contents

1. [Pre-Deployment Checklist](#pre-deployment-checklist)
2. [Systemd Service Installation](#systemd-service-installation)
3. [Hardware Validation](#hardware-validation)
4. [Performance Validation](#performance-validation)
5. [Production Configuration](#production-configuration)
6. [Release Process](#release-process)
7. [Maintenance](#maintenance)
8. [Rollback Procedure](#rollback-procedure)

---

## Pre-Deployment Checklist

### Software Dependencies

- [ ] Python 3.9+ installed
  ```bash
  python3 --version  # Should be ≥3.9
  ```

- [ ] Virtual environment created and activated
  ```bash
  cd /home/pi/esp32_btaudio/rpi_i2s_source
  source venv/bin/activate
  ```

- [ ] All Python dependencies installed
  ```bash
  pip install -r requirements.txt
  pip list | grep -E "Flask|numpy|scipy|pyserial|pyyaml|psutil"
  ```

- [ ] ALSA utils installed (for I2S)
  ```bash
  dpkg -l | grep alsa-utils
  ```

### Configuration Files

- [ ] `config.yaml` exists and validated
  ```bash
  ls -l config.yaml
  python -c "import yaml; yaml.safe_load(open('config.yaml'))"
  ```

- [ ] Audio directory exists
  ```bash
  ls -ld /home/pi/audio
  ```

- [ ] Paths in `config.yaml` correct for production:
  ```yaml
  paths:
    audio_dir: "/home/pi/audio"
    logs_dir: "/var/log/rpi-i2s-source"  # Optional
  ```

### Hardware Configuration

- [ ] UART enabled and accessible
  ```bash
  ls -l /dev/serial0
  # Should show: lrwxrwxrwx ... /dev/serial0 -> ttyAMA0
  ```

- [ ] User in `dialout` group
  ```bash
  groups | grep dialout
  ```

- [ ] I2S device available
  ```bash
  aplay -l | grep -i i2s
  # Should show snd_rpi_i2s or similar
  ```

- [ ] UART not used by Bluetooth
  ```bash
  grep "dtoverlay=disable-bt" /boot/config.txt
  # Should show the line
  ```

- [ ] I2S enabled
  ```bash
  grep "i2s" /boot/config.txt
  # Should show: dtoverlay=i2s-mmap or dtparam=i2s=on
  ```

### Hardware Wiring

- [ ] I2S connections verified:
  - RPi GPIO18 → ESP32 BCLK
  - RPi GPIO19 → ESP32 WS
  - RPi GPIO21 → ESP32 DIN
  - GND connected

- [ ] UART connections verified:
  - RPi GPIO14 (TX) → ESP32 RX
  - RPi GPIO15 (RX) → ESP32 TX
  - GND connected

- [ ] Wire length <30 cm (I2S signals)

- [ ] ESP32 powered and running esp_bt_audio_source firmware

### Testing

- [ ] All unit tests passing
  ```bash
  pytest tests/ -v
  # Expected: 206 passed
  ```

- [ ] Application starts without errors
  ```bash
  python main.py
  # Should see: "Web server starting on http://0.0.0.0:5000"
  # Press Ctrl+C to stop
  ```

- [ ] Web UI accessible
  ```bash
  curl http://localhost:5000/status
  # Should return JSON status
  ```

---

## Systemd Service Installation

### Step 1: Copy Service File

```bash
sudo cp /home/pi/esp32_btaudio/rpi_i2s_source/rpi-i2s-source.service \
        /etc/systemd/system/rpi-i2s-source.service
```

### Step 2: Verify Service File

```bash
cat /etc/systemd/system/rpi-i2s-source.service
```

Ensure paths are correct:
- `WorkingDirectory=/home/pi/esp32_btaudio/rpi_i2s_source`
- `ExecStart=/home/pi/esp32_btaudio/rpi_i2s_source/venv/bin/python main.py`
- `User=pi`

### Step 3: Reload Systemd

```bash
sudo systemctl daemon-reload
```

### Step 4: Enable Service (Auto-Start on Boot)

```bash
sudo systemctl enable rpi-i2s-source
```

Expected output:
```
Created symlink /etc/systemd/system/multi-user.target.wants/rpi-i2s-source.service → /etc/systemd/system/rpi-i2s-source.service.
```

### Step 5: Start Service

```bash
sudo systemctl start rpi-i2s-source
```

### Step 6: Verify Service Status

```bash
sudo systemctl status rpi-i2s-source
```

Expected output:
```
● rpi-i2s-source.service - Raspberry Pi I2S Audio Test Jig
     Loaded: loaded (/etc/systemd/system/rpi-i2s-source.service; enabled; vendor preset: enabled)
     Active: active (running) since Thu 2026-02-06 12:00:00 PST; 5s ago
   Main PID: 1234 (python)
      Tasks: 5 (limit: 4164)
     Memory: 45.2M
        CPU: 1.234s
     CGroup: /system.slice/rpi-i2s-source.service
             └─1234 /home/pi/esp32_btaudio/rpi_i2s_source/venv/bin/python main.py

Feb 06 12:00:00 rpi-i2s-source systemd[1]: Started Raspberry Pi I2S Audio Test Jig.
Feb 06 12:00:00 rpi-i2s-source python[1234]: INFO:main:Starting Raspberry Pi I2S Source...
Feb 06 12:00:00 rpi-i2s-source python[1234]: INFO:main:Configuration loaded from config.yaml
Feb 06 12:00:00 rpi-i2s-source python[1234]: INFO:main:Web server starting on http://0.0.0.0:5000
```

### Step 7: View Logs

```bash
# Real-time logs
sudo journalctl -u rpi-i2s-source -f

# Last 50 lines
sudo journalctl -u rpi-i2s-source -n 50

# Since last boot
sudo journalctl -u rpi-i2s-source -b
```

### Step 8: Test Auto-Start

```bash
sudo reboot
```

After reboot (~30 seconds):
```bash
sudo systemctl status rpi-i2s-source
# Should show: Active: active (running)

curl http://localhost:5000/status
# Should return JSON status
```

---

## Hardware Validation

### I2S Signal Verification

**Option 1: Logic Analyzer (Recommended)**

See [TESTING.md](TESTING.md) Manual Hardware Validation section for detailed procedure.

**Quick check:**
- BCLK: 1.536 MHz (64 × 48 kHz)
- WS: 48 kHz
- BCLK/WS ratio: 64:1
- Phase: WS transitions on BCLK falling edge

**Option 2: Oscilloscope**

- Probe GPIO18 (BCLK): Should see ~1.5 MHz square wave
- Probe GPIO19 (WS): Should see 48 kHz square wave
- Verify 3.3V logic levels

### UART Communication Test

**1. Send test command via web UI:**

Open browser: `http://<rpi-ip>:5000`

Navigate to Bluetooth tab, click "SCAN"

**2. Check UART logs:**
```bash
sudo journalctl -u rpi-i2s-source -n 20 | grep UART
```

Should show:
```
INFO:uart:Sent command: SCAN
INFO:uart:Received response: OK
```

**3. Test from command line:**
```bash
# Using curl to send UART command
curl -X POST http://localhost:5000/uart/send \
     -H "Content-Type: application/json" \
     -d '{"command": "STATUS"}'
```

Expected response:
```json
{
  "status": "success",
  "command": "STATUS",
  "response": "BT_IDLE"
}
```

### Bluetooth Audio Test

**1. Connect Bluetooth speaker:**
- In web UI, click "SCAN"
- Wait for devices to appear
- Click "CONNECT" next to your speaker
- Wait for connection (10-20 seconds)

**2. Generate test tone:**
- Select "Tone" source
- Set frequency: 1000 Hz
- Set amplitude: 50%
- Click "Start"

**3. Start Bluetooth playback:**
- Click "START" in Bluetooth panel

**4. Verify audio:**
- Listen to Bluetooth speaker for 1 kHz tone
- Tone should be clear, no distortion or dropouts

---

## Performance Validation

### CPU Usage Validation

**Run performance tests:**
```bash
cd /home/pi/esp32_btaudio/rpi_i2s_source
source venv/bin/activate
pytest tests/performance/test_cpu_usage.py -v --run-hardware
```

**Expected results:**
- Idle CPU: <10% average ✅
- Tone generation: <25% average ✅
- WAV playback: <30% average ✅

**Manual monitoring:**
```bash
# Monitor for 5 minutes
python tests/performance/monitor_resources.py --duration=300 --output=cpu_test.csv
```

Review CSV for CPU spikes or sustained high usage.

### Memory Usage Validation

**Run memory tests:**
```bash
pytest tests/performance/test_memory_usage.py -v --run-hardware
```

**Expected results:**
- Baseline: <100 MB RSS ✅
- 5-minute stability: <1 MB/min growth ✅

**Manual monitoring:**
```bash
# Monitor for 1 hour
python tests/performance/monitor_resources.py --duration=3600 --interval=60 --output=memory_1hr.csv
```

Check for memory leaks (continuous growth >1 MB/min).

### I2S Underruns Check

**During 1-hour stability test:**
```bash
pytest tests/integration/test_long_duration.py::TestLongDuration::test_one_hour_stability -v --run-hardware
```

**Expected:**
- Underruns: <1000/hour ✅
- No audio dropouts

**Or monitor via web UI:**
- Open dashboard
- Watch I2S status panel
- Underruns should remain near zero during active playback

### Network Latency Check

**Test web UI responsiveness:**
```bash
# Measure page load time
time curl http://localhost:5000/ > /dev/null
```

**Expected:** <500 ms

**Test API latency:**
```bash
# Generate tone (10 requests)
for i in {1..10}; do
  time curl -X POST http://localhost:5000/tone \
       -H "Content-Type: application/json" \
       -d '{"frequency": 1000, "duration": 5, "amplitude": 0.5}' 2>&1 | grep real
done
```

**Expected:** <100 ms per request

---

## Production Configuration

### Logging Configuration

**Option 1: Log to systemd journal (default)**

Service already configured for journal logging. View with:
```bash
sudo journalctl -u rpi-i2s-source -f
```

**Option 2: Log to file**

Edit `/etc/systemd/system/rpi-i2s-source.service`:
```ini
[Service]
StandardOutput=append:/var/log/rpi-i2s-source/app.log
StandardError=append:/var/log/rpi-i2s-source/error.log
```

Create log directory:
```bash
sudo mkdir -p /var/log/rpi-i2s-source
sudo chown pi:pi /var/log/rpi-i2s-source
```

Reload and restart:
```bash
sudo systemctl daemon-reload
sudo systemctl restart rpi-i2s-source
```

### Log Rotation

Create `/etc/logrotate.d/rpi-i2s-source`:
```bash
sudo nano /etc/logrotate.d/rpi-i2s-source
```

Content:
```
/var/log/rpi-i2s-source/*.log {
    daily
    rotate 7
    compress
    delaycompress
    missingok
    notifempty
    create 0644 pi pi
}
```

### Security Hardening (Optional)

**Firewall (ufw):**
```bash
sudo apt install ufw
sudo ufw allow 5000/tcp comment 'rpi-i2s-source web UI'
sudo ufw enable
```

**systemd security options:**

Edit `/etc/systemd/system/rpi-i2s-source.service`:
```ini
[Service]
# Security hardening
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/home/pi/audio /var/log/rpi-i2s-source
NoNewPrivileges=true
PrivateTmp=true
```

**Note:** These options may cause issues with I2S/UART access. Test thoroughly.

---

## Release Process

### Version Tagging

**1. Update version in code:**

Edit `main.py` or create `version.py`:
```python
__version__ = "1.0.0"
```

**2. Commit version bump:**
```bash
git add main.py  # or version.py
git commit -m "Bump version to 1.0.0"
```

**3. Create Git tag:**
```bash
git tag -a v1.0.0 -m "MVP Release v1.0.0

Release Notes:
- Complete I2S master implementation with 48 kHz stereo audio
- UART control interface for esp_bt_audio_source
- Web UI with real-time telemetry
- 206 unit tests (100% passing)
- 7 integration tests (hardware validation)
- 9 performance tests (NFR validation)
- Comprehensive documentation (README, SETUP, TESTING)
- Systemd service for auto-start

Requirements:
- Raspberry Pi 3B+ or newer
- Python 3.9+
- esp_bt_audio_source firmware on ESP32
"
```

**4. Push tag to GitHub:**
```bash
git push origin master
git push origin v1.0.0
```

### Release Archive

**Create release tarball:**
```bash
cd /home/pi/esp32_btaudio
tar -czf rpi_i2s_source_v1.0.0.tar.gz \
    rpi_i2s_source/ \
    --exclude='rpi_i2s_source/venv' \
    --exclude='rpi_i2s_source/__pycache__' \
    --exclude='rpi_i2s_source/.pytest_cache' \
    --exclude='rpi_i2s_source/htmlcov' \
    --exclude='rpi_i2s_source/.coverage'
```

**Verify archive:**
```bash
tar -tzf rpi_i2s_source_v1.0.0.tar.gz | head -20
```

**Upload to GitHub Release:**
1. Go to: https://github.com/ekkus93/esp32_btaudio/releases
2. Click "Draft a new release"
3. Tag: `v1.0.0`
4. Title: `Raspberry Pi I2S Source v1.0.0 - MVP Release`
5. Attach: `rpi_i2s_source_v1.0.0.tar.gz`
6. Publish release

---

## Maintenance

### Service Management

**Stop service:**
```bash
sudo systemctl stop rpi-i2s-source
```

**Start service:**
```bash
sudo systemctl start rpi-i2s-source
```

**Restart service:**
```bash
sudo systemctl restart rpi-i2s-source
```

**Disable auto-start:**
```bash
sudo systemctl disable rpi-i2s-source
```

**Re-enable auto-start:**
```bash
sudo systemctl enable rpi-i2s-source
```

### Updates and Patches

**1. Stop service:**
```bash
sudo systemctl stop rpi-i2s-source
```

**2. Pull latest code:**
```bash
cd /home/pi/esp32_btaudio
git pull origin master
```

**3. Update dependencies (if needed):**
```bash
cd rpi_i2s_source
source venv/bin/activate
pip install --upgrade -r requirements.txt
```

**4. Run tests:**
```bash
pytest tests/ -v
```

**5. Restart service:**
```bash
sudo systemctl restart rpi-i2s-source
```

**6. Verify:**
```bash
sudo systemctl status rpi-i2s-source
curl http://localhost:5000/status
```

### Monitoring

**Check service health:**
```bash
# Service status
sudo systemctl status rpi-i2s-source

# Recent logs
sudo journalctl -u rpi-i2s-source -n 50

# Resource usage
python tests/performance/monitor_resources.py --process=main.py --duration=60
```

**Automated monitoring (cron):**

Create `/home/pi/monitor_health.sh`:
```bash
#!/bin/bash
# Check if service is running
if ! systemctl is-active --quiet rpi-i2s-source; then
    echo "$(date): Service is down! Restarting..." >> /var/log/rpi-i2s-health.log
    sudo systemctl start rpi-i2s-source
fi
```

Make executable:
```bash
chmod +x /home/pi/monitor_health.sh
```

Add to crontab:
```bash
crontab -e
# Add this line:
*/5 * * * * /home/pi/monitor_health.sh
```

---

## Rollback Procedure

### Scenario: Update Breaks Functionality

**1. Identify last working version:**
```bash
cd /home/pi/esp32_btaudio
git log --oneline -10
# Find last known good commit
```

**2. Stop service:**
```bash
sudo systemctl stop rpi-i2s-source
```

**3. Rollback code:**
```bash
git checkout <commit-hash>  # or git checkout v1.0.0
```

**4. Reinstall dependencies (if changed):**
```bash
cd rpi_i2s_source
source venv/bin/activate
pip install -r requirements.txt
```

**5. Test manually:**
```bash
python main.py
# Verify it starts correctly
# Press Ctrl+C
```

**6. Restart service:**
```bash
sudo systemctl start rpi-i2s-source
sudo systemctl status rpi-i2s-source
```

**7. Verify functionality:**
```bash
curl http://localhost:5000/status
# Test web UI and audio generation
```

### Scenario: Service Won't Start

**1. Check logs:**
```bash
sudo journalctl -u rpi-i2s-source -n 100
```

**2. Common issues:**

- **Python import errors:** Reinstall dependencies
  ```bash
  cd /home/pi/esp32_btaudio/rpi_i2s_source
  source venv/bin/activate
  pip install -r requirements.txt
  ```

- **Config file errors:** Validate config.yaml
  ```bash
  python -c "import yaml; yaml.safe_load(open('config.yaml'))"
  ```

- **I2S/UART device missing:** Check hardware setup (see SETUP.md)

**3. Test manually:**
```bash
cd /home/pi/esp32_btaudio/rpi_i2s_source
source venv/bin/activate
python main.py
# Review error messages
```

---

## Deployment Checklist Summary

**Pre-Deployment:**
- [ ] All dependencies installed
- [ ] UART and I2S configured in `/boot/config.txt`
- [ ] config.yaml validated
- [ ] Unit tests passing (206/206)
- [ ] Application starts manually without errors

**Systemd Service:**
- [ ] Service file installed to `/etc/systemd/system/`
- [ ] Service enabled for auto-start
- [ ] Service started successfully
- [ ] Service auto-starts after reboot

**Hardware Validation:**
- [ ] I2S signals verified (logic analyzer or oscilloscope)
- [ ] UART communication working
- [ ] Bluetooth audio playback working

**Performance Validation:**
- [ ] CPU usage within targets (<25% active, <10% idle)
- [ ] Memory usage within target (<100 MB RSS)
- [ ] No memory leaks (<1 MB/min growth)
- [ ] I2S underruns minimal (<1000/hour)

**Production Ready:**
- [ ] Logging configured
- [ ] Service runs for 1+ hour without issues
- [ ] Web UI accessible remotely
- [ ] Documentation complete and accurate

---

## Support

For deployment issues:
1. Review logs: `sudo journalctl -u rpi-i2s-source -n 100`
2. Check [SETUP.md](SETUP.md) troubleshooting section
3. Check [TESTING.md](TESTING.md) for hardware validation
4. Report issues: https://github.com/ekkus93/esp32_btaudio/issues

---

**Deployment Status:** Ready for production use after validation ✅
