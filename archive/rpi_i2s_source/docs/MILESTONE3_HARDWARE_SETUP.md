# Milestone 3: Flask Web UI — Hardware Setup Guide

## Overview

This guide shows how to deploy and validate the Flask web UI for the RPi I2S Source project. The web interface provides:
- **Dashboard**: Audio source control (tone, sweep, WAV playback)
- **Bluetooth Control**: UART commands for Bluetooth connectivity
- **Real-time Monitoring**: Server-Sent Events (SSE) for live status updates

**Milestone 3 Success Criteria:**
- ✅ Web UI accessible from laptop on same LAN (`http://<rpi-ip>:5000`)
- ✅ Tone frequency slider changes audio in <200 ms (user-perceived latency)
- ✅ `SCAN` button triggers scan, results appear in device list within 10 seconds
- ✅ Status panel updates connection state when Bluetooth device connects/disconnects

---

## Prerequisites

- Raspberry Pi with Raspberry Pi OS installed (tested on Bullseye/Bookworm)
- Network connectivity (Ethernet or Wi-Fi)
- Python 3.7+ with pip
- Project dependencies installed (Flask, requests)

---

## Step 1: Install Python Dependencies

### 1.1. Update System Packages

```bash
sudo apt update
sudo apt upgrade -y
```

### 1.2. Install Python 3 and pip

```bash
sudo apt install -y python3 python3-pip python3-venv
```

### 1.3. Create Virtual Environment (Recommended)

```bash
cd ~/esp32_btaudio/rpi_i2s_source
python3 -m venv venv
source venv/bin/activate
```

### 1.4. Install Project Dependencies

```bash
# Install from requirements.txt (if available)
pip install -r requirements.txt

# Or install manually
pip install flask pyyaml
pip install requests  # For test script
```

---

## Step 2: Configure Network Access

### 2.1. Find Raspberry Pi IP Address

```bash
# Ethernet
ip addr show eth0 | grep "inet "

# Wi-Fi
ip addr show wlan0 | grep "inet "

# Or use hostname (if mDNS is working)
hostname -I
```

**Example output:**
```
inet 192.168.1.100/24 brd 192.168.1.255 scope global dynamic noprefixall eth0
```

Your Raspberry Pi IP is `192.168.1.100`.

### 2.2. Configure Flask Server Binding

The Flask server needs to bind to `0.0.0.0` to accept connections from other devices on the LAN.

**Edit `config.yaml`:**

```bash
nano config.yaml
```

**Find the `web` section and set:**

```yaml
web:
  bind_address: "0.0.0.0"  # Listen on all network interfaces
  port: 5000                # Default Flask port
  debug: false              # Disable debug mode for production
```

**Save and exit** (Ctrl+O, Enter, Ctrl+X).

### 2.3. Configure Firewall (if enabled)

If you're using `ufw` or `iptables`, allow incoming connections on port 5000:

```bash
# Check if ufw is active
sudo ufw status

# If active, allow port 5000
sudo ufw allow 5000/tcp
sudo ufw reload
```

**Verify the rule:**

```bash
sudo ufw status numbered
```

**Expected:**
```
To                         Action      From
--                         ------      ----
5000/tcp                   ALLOW       Anywhere
```

---

## Step 3: Start the Flask Web Server

### 3.1. Start Full Application

```bash
cd ~/esp32_btaudio/rpi_i2s_source
source venv/bin/activate  # If using virtual environment
python3 main.py
```

**Expected output:**

```
2026-02-06 14:00:00 - INFO - ConfigManager initialized (config.yaml)
2026-02-06 14:00:00 - INFO - RingBuffer initialized (size=8192)
2026-02-06 14:00:00 - INFO - AudioEngine initialized
2026-02-06 14:00:00 - INFO - I2SDriverALSA initialized (device=hw:0,0, sample_rate=48000)
2026-02-06 14:00:00 - INFO - Started AudioEngine
2026-02-06 14:00:00 - INFO - Started I2SDriver
2026-02-06 14:00:00 - INFO - Starting Flask server on 0.0.0.0:5000
 * Serving Flask app 'app'
 * Running on all addresses (0.0.0.0)
 * Running on http://127.0.0.1:5000
 * Running on http://192.168.1.100:5000
```

**Note the LAN URL**: `http://192.168.1.100:5000` (your IP will differ)

### 3.2. Test from Raspberry Pi (localhost)

Open a second terminal and test:

```bash
curl http://localhost:5000/api/status
```

**Expected:** JSON response with system status.

```json
{
  "i2s": {
    "active": true,
    "sample_rate": 48000,
    ...
  },
  "audio": {
    "source": "tone",
    "tone_freq": 1000,
    ...
  },
  ...
}
```

---

## Step 4: Access from Laptop (LAN)

### 4.1. Open Web Browser

From any device on the same LAN, open a web browser and navigate to:

```
http://<raspberry-pi-ip>:5000
```

**Example:**
```
http://192.168.1.100:5000
```

### 4.2. Verify Web UI Loads

You should see the **RPi I2S Audio Source Dashboard** with:
- **Audio Source** tabs (Tone, Sweep, WAV File, Silence)
- **Tone Generator** sliders (Frequency, Amplitude)
- **Status Panel** (I2S Active, Buffer Fill, Uptime)
- **Bluetooth Control** section (SCAN, CONNECT, DISCONNECT)

### 4.3. Test Tone Control Latency

**Milestone 3 Success Criterion:** Tone frequency slider changes audio in <200 ms.

1. **Set audio source** to **Tone**
2. **Move the frequency slider** (20 Hz → 20 kHz)
3. **Observe latency:**
   - Audio should change almost immediately
   - Target: <200ms user-perceived latency
   - Typical: 10-50ms (measured via API calls)

### 4.4. Test Real-Time Status Updates (SSE)

The status panel should update every **500ms** (2 Hz) via Server-Sent Events:

1. **Watch the status panel** (top right)
2. **Verify updates:**
   - Uptime increments every second
   - Buffer fill % changes dynamically
   - I2S Active state reflects reality

**To verify SSE stream directly:**

```bash
# From laptop or Raspberry Pi
curl -N http://192.168.1.100:5000/api/stream
```

**Expected:** Continuous stream of `data: {...}` lines every 500ms.

---

## Step 5: Run Automated Validation Test

The `milestone3_web_ui_test.py` script validates all Milestone 3 success criteria.

### 5.1. From Raspberry Pi (localhost)

```bash
cd ~/esp32_btaudio/rpi_i2s_source
./milestone3_web_ui_test.py
```

### 5.2. From Laptop (LAN)

**Install Python and requests:**

```bash
pip install requests
```

**Run test against Raspberry Pi:**

```bash
./milestone3_web_ui_test.py --host 192.168.1.100
```

### 5.3. Expected Test Output

```
======================================================================
MILESTONE 3: Flask Web UI Validation Test
======================================================================

Test 1: Checking server connectivity...
  Server responded: HTTP 200
  Content-Type: text/html; charset=utf-8
✓ Server connectivity test PASSED

Test 2: Checking web UI pages...
  ✓ Dashboard (/): OK
✓ Web UI pages test PASSED

Test 3: Testing REST API endpoints...
  ✓ GET /api/status: OK (i2s=True)
  ✓ POST /api/tone: OK (440 Hz, 50%)
  ✓ POST /api/silence: OK
✓ REST API test PASSED

Test 4: Testing tone control latency (<200ms)...
  Testing 5 tone changes...
  1000 Hz: 12.3 ms
  440 Hz: 15.7 ms
  2000 Hz: 11.9 ms
  500 Hz: 14.2 ms
  1500 Hz: 13.1 ms

  Average latency: 13.4 ms
  Maximum latency: 15.7 ms
  Threshold: 200 ms
  ✓ Latency requirement met (13.4 ms < 200 ms)
✓ Tone control latency test PASSED

Test 5: Testing Server-Sent Events stream...
  Connecting to SSE stream: http://192.168.1.100:5000/api/stream
  SSE stream connected: text/event-stream
  Update 1: First update
  Update 2: Interval = 503 ms
  Update 3: Interval = 498 ms

  Average interval: 501 ms (expected ~500 ms)
  ✓ SSE update rate OK
  ✓ Received 3 SSE updates
✓ SSE stream test PASSED

======================================================================
MILESTONE 3 TEST RESULTS
======================================================================
Tests Run:    5
Tests Passed: 5
Tests Failed: 0
API Calls:    11
API Errors:   0
Avg Latency:  13.4 ms

✓✓✓ ALL MILESTONE 3 SUCCESS CRITERIA MET ✓✓✓

Milestone 3 Deliverables Validated:
  ✓ Flask web server accessible on LAN
  ✓ All web UI pages load correctly
  ✓ REST API endpoints function properly
  ✓ Tone control latency <200ms
  ✓ Server-Sent Events stream working
======================================================================
```

---

## Step 6: Test Bluetooth Control (Optional)

If you have UART connected to ESP32:

### 6.1. Click "SCAN" Button

1. Open web UI in browser
2. Navigate to **Bluetooth Control** section
3. Click **SCAN** button
4. **Verify:** Scan results appear within 10 seconds

### 6.2. Connect to Bluetooth Device

1. Select device from scan results
2. Click **CONNECT** button
3. **Verify:** Status panel updates to show "BT Connected"

### 6.3. Monitor Status Updates

**Milestone 3 Success Criterion:** Status panel updates connection state when Bluetooth device connects/disconnects.

- **Connect device:** Status changes to "Connected"
- **Disconnect device:** Status changes to "Disconnected"
- **Updates via SSE:** Real-time, no page refresh needed

---

## Step 7: Verification Checklist

Use this checklist to confirm Milestone 3 success criteria:

- [ ] **Web UI Accessible from LAN**
  - [ ] Can open `http://<rpi-ip>:5000` from laptop browser
  - [ ] Dashboard page loads correctly
  - [ ] All controls visible and functional

- [ ] **Tone Control Latency <200ms**
  - [ ] Frequency slider changes audio immediately
  - [ ] No perceptible lag (<200ms user experience)
  - [ ] Test script confirms latency <200ms

- [ ] **Bluetooth Control (if UART available)**
  - [ ] SCAN button triggers Bluetooth scan
  - [ ] Scan results appear within 10 seconds
  - [ ] Device list populates correctly

- [ ] **Real-Time Status Updates**
  - [ ] Status panel updates every 500ms
  - [ ] SSE stream delivers status updates
  - [ ] Connection state updates when BT connects/disconnects
  - [ ] No manual refresh required

- [ ] **REST API Functionality**
  - [ ] `GET /api/status` returns JSON
  - [ ] `POST /api/tone` changes audio
  - [ ] `POST /api/sweep` starts frequency sweep
  - [ ] `POST /api/silence` mutes audio

---

## Troubleshooting

### Issue 1: Cannot Access Web UI from Laptop

**Symptoms:**
- Browser shows "Connection refused" or "Unable to connect"
- `curl http://<rpi-ip>:5000` fails

**Solutions:**

1. **Verify Flask is running:**
   ```bash
   ps aux | grep main.py
   ```
   If not running, start it: `python3 main.py`

2. **Check bind address in `config.yaml`:**
   ```yaml
   web:
     bind_address: "0.0.0.0"  # NOT "127.0.0.1"
   ```

3. **Verify firewall rules:**
   ```bash
   sudo ufw status
   sudo ufw allow 5000/tcp
   ```

4. **Test from Raspberry Pi first:**
   ```bash
   curl http://localhost:5000/api/status
   ```

5. **Check network connectivity:**
   ```bash
   ping 192.168.1.100  # From laptop
   ```

### Issue 2: Status Panel Not Updating

**Symptoms:**
- Status panel shows stale data
- Uptime doesn't increment
- SSE stream not working

**Solutions:**

1. **Verify SSE endpoint:**
   ```bash
   curl -N http://192.168.1.100:5000/api/stream
   ```
   Should see continuous `data: {...}` lines.

2. **Check browser console:**
   - Open Developer Tools (F12)
   - Look for errors in Console tab
   - Check Network tab for `/api/stream` connection

3. **Test with different browser:**
   - Some browsers handle SSE differently
   - Try Chrome, Firefox, or Edge

4. **Restart Flask server:**
   ```bash
   # Stop: Ctrl+C
   # Restart: python3 main.py
   ```

### Issue 3: Tone Control Latency Too High

**Symptoms:**
- Frequency slider takes >1 second to change audio
- Test script reports latency >200ms

**Solutions:**

1. **Check CPU load:**
   ```bash
   top
   ```
   If CPU is maxed, close other applications.

2. **Verify I2S is running:**
   ```bash
   curl http://localhost:5000/api/status | grep '"active": true'
   ```

3. **Reduce buffer size (if needed):**
   Edit `config.yaml`:
   ```yaml
   ring_buffer:
     size_frames: 4096  # Smaller = lower latency, higher underrun risk
   ```

4. **Check network latency (if testing from laptop):**
   ```bash
   ping -c 10 192.168.1.100
   ```
   High network latency adds to total latency.

### Issue 4: Bluetooth Commands Not Working

**Symptoms:**
- SCAN button does nothing
- API returns `503 Service Unavailable`

**Solutions:**

1. **UART manager is optional:**
   - Flask server works without UART
   - Returns `503` if UART not available

2. **Check UART configuration:**
   See [MILESTONE2_HARDWARE_SETUP.md](MILESTONE2_HARDWARE_SETUP.md) for UART setup.

3. **Verify UART manager is enabled:**
   ```bash
   # Check main.py logs for:
   # "Started UARTCommandManager"
   ```

4. **Test UART directly:**
   ```bash
   python3 milestone2_uart_test.py
   ```

### Issue 5: Port 5000 Already in Use

**Symptoms:**
- Flask fails to start
- Error: `Address already in use`

**Solutions:**

1. **Find process using port 5000:**
   ```bash
   sudo lsof -i :5000
   ```

2. **Kill the process:**
   ```bash
   kill -9 <PID>
   ```

3. **Or use a different port:**
   Edit `config.yaml`:
   ```yaml
   web:
     port: 8080
   ```
   Then access: `http://<rpi-ip>:8080`

---

## Advanced Testing

### Manual API Testing with curl

**Get status:**
```bash
curl http://192.168.1.100:5000/api/status | jq
```

**Set tone to 1 kHz:**
```bash
curl -X POST http://192.168.1.100:5000/api/tone \
  -H "Content-Type: application/json" \
  -d '{"freq": 1000, "amp": 0.75, "mode": "mono"}'
```

**Start frequency sweep:**
```bash
curl -X POST http://192.168.1.100:5000/api/sweep \
  -H "Content-Type: application/json" \
  -d '{"duration": 10, "loop": false}'
```

**Set silence:**
```bash
curl -X POST http://192.168.1.100:5000/api/silence
```

**Bluetooth scan (if UART available):**
```bash
curl -X POST http://192.168.1.100:5000/api/bt/command \
  -H "Content-Type: application/json" \
  -d '{"command": "SCAN", "args": ""}'
```

### SSE Stream Testing

**Monitor SSE with curl:**
```bash
curl -N http://192.168.1.100:5000/api/stream
```

**Expected output:**
```
data: {"i2s": {"active": true, ...}, "audio": {...}, ...}

data: {"i2s": {"active": true, ...}, "audio": {...}, ...}

data: {"i2s": {"active": true, ...}, "audio": {...}, ...}
```

Press Ctrl+C to stop.

### Python API Testing

```python
import requests

# Get status
response = requests.get('http://192.168.1.100:5000/api/status')
status = response.json()
print(f"I2S Active: {status['i2s']['active']}")

# Set tone
response = requests.post(
    'http://192.168.1.100:5000/api/tone',
    json={'freq': 440, 'amp': 0.5, 'mode': 'mono'}
)
print(response.json())
```

---

## Production Deployment (Optional)

For production use, consider using a production WSGI server instead of Flask's built-in server.

### Using Gunicorn

**Install:**
```bash
pip install gunicorn
```

**Run:**
```bash
gunicorn -w 4 -b 0.0.0.0:5000 'web.app:create_app()'
```

### Using systemd Service

**Create service file:**

```bash
sudo nano /etc/systemd/system/rpi-i2s-source.service
```

**Content:**

```ini
[Unit]
Description=RPi I2S Audio Source Web UI
After=network.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/esp32_btaudio/rpi_i2s_source
Environment="PATH=/home/pi/esp32_btaudio/rpi_i2s_source/venv/bin"
ExecStart=/home/pi/esp32_btaudio/rpi_i2s_source/venv/bin/python3 main.py
Restart=always

[Install]
WantedBy=multi-user.target
```

**Enable and start:**

```bash
sudo systemctl daemon-reload
sudo systemctl enable rpi-i2s-source
sudo systemctl start rpi-i2s-source
```

**Check status:**

```bash
sudo systemctl status rpi-i2s-source
```

---

## Success Criteria Summary

**Milestone 3 deliverables are complete when:**

✅ **1. Web UI Accessible from LAN**
- Can open `http://<rpi-ip>:5000` from any device on the network
- Dashboard loads with all controls visible

✅ **2. Tone Control Latency <200ms**
- Frequency slider changes audio immediately
- Average latency <200ms (typically 10-50ms)
- Validated via `milestone3_web_ui_test.py`

✅ **3. Bluetooth Control Functional** (if UART available)
- SCAN button triggers scan
- Results appear within 10 seconds
- Device list populates correctly

✅ **4. Real-Time Status Updates**
- Status panel updates every 500ms via SSE
- Connection state reflects reality
- No manual page refresh required

---

## Next Steps

After validating Milestone 3:

1. **Milestone 4**: Advanced Audio Sources (frequency sweeps, WAV playback)
   - Already implemented in software
   - Test WAV file playback from web UI
   - Verify frequency sweep works correctly

2. **Milestone 5**: Stability & Telemetry
   - Run long-duration tests (24+ hours)
   - Monitor buffer underruns and CPU usage
   - Verify graceful error handling

3. **Integration Testing**:
   - Test full system: I2S + UART + Web UI + ESP32
   - Verify end-to-end Bluetooth connectivity
   - Test concurrent users on web UI

---

## References

- **PRD**: `docs/PRD.md` (Section 13 - Success Criteria)
- **Functional Spec**: `docs/FS.md` (Section 2.1 - Flask Web Server)
- **TODO**: `docs/TODO.md` (Milestone 3)
- **Milestone 1 Guide**: `docs/MILESTONE1_HARDWARE_SETUP.md`
- **Milestone 2 Guide**: `docs/MILESTONE2_HARDWARE_SETUP.md`

---

## Contact & Support

For issues, questions, or contributions:
- **GitHub**: https://github.com/ekkus93/esp32_btaudio
- **Documentation**: `docs/` directory
- **Tests**: `tests/` directory
