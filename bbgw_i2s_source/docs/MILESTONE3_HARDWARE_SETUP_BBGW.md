# Milestone 3: Flask Web UI — Hardware Setup (BBGW)

**Project:** BeagleBone Green Wireless I2S Audio Source  
**Milestone:** Phase 3.4 — Flask Web UI Validation  
**Date:** 2026-02-07  
**Hardware:** BeagleBone Green Wireless + Laptop/Smartphone

---

## Overview

This document describes the hardware setup and testing procedures for **Milestone 3**: Flask Web UI testing for the BeagleBone Green Wireless I2S Audio Source project.

**Milestone 3 Objectives:**
1. Deploy Flask web server on BBGW accessible via LAN
2. Validate REST API endpoints for audio control
3. Test Server-Sent Events (SSE) for real-time status updates
4. Verify tone control latency < 200ms
5. Ensure web UI is accessible from browsers on LAN

**Success Criteria:**
- [x] Flask server runs on BBGW and responds on port 5000
- [x] Web UI accessible from laptop/phone browser: `http://<bbgw-ip>:5000`
- [x] All REST API endpoints function correctly
- [x] Tone frequency changes have average latency <200ms
- [x] SSE stream delivers status updates at ~2 Hz (500ms intervals)
- [x] Dashboard, Bluetooth control, and Logs pages all load correctly

---

## Table of Contents

1. [Hardware Requirements](#hardware-requirements)
2. [Network Configuration](#network-configuration)
3. [Software Dependencies](#software-dependencies)
4. [Flask Server Deployment](#flask-server-deployment)
5. [Running Milestone 3 Test](#running-milestone-3-test)
6. [Manual Browser Testing](#manual-browser-testing)
7. [Expected Results](#expected-results)
8. [Troubleshooting](#troubleshooting)
9. [Success Validation](#success-validation)

---

## Hardware Requirements

### BeagleBone Green Wireless
- **Board:** BeagleBone Green Wireless (BBGW)
- **OS:** Debian 11 or later (Linux kernel 5.10+)
- **Network:** Wi-Fi or Ethernet
  - Wi-Fi: Built-in WL1835MOD module
  - Ethernet: USB Ethernet adapter (optional)
- **Power:** USB or 5V barrel jack (stable power required for Wi-Fi)
- **Storage:** 4GB+ eMMC or microSD card

### Client Device (Laptop or Smartphone)
- **Requirements:**
  - Connected to same LAN as BBGW
  - Modern web browser (Chrome, Firefox, Safari, Edge)
  - Python 3.8+ with `requests` library (for automated test script)
  - SSH client (for remote control, optional)

### Network Infrastructure
- **Router/Access Point:** Wi-Fi or wired LAN
- **DHCP:** Enabled (automatic IP assignment)
- **Firewall:** Allow TCP port 5000 (Flask default)

---

## Network Configuration

### Step 1: Configure BBGW Wi-Fi

**Method 1: Using connmanctl (Recommended)**

```bash
# SSH into BBGW
ssh debian@beaglebone.local
# Default password: temppwd

# Start connmanctl interactive mode
sudo connmanctl

# Inside connmanctl:
connmanctl> enable wifi
connmanctl> scan wifi
connmanctl> services
# Note the service ID for your SSID (e.g., wifi_xxxxxxxxxxxx_managed_psk)

connmanctl> agent on
connmanctl> connect wifi_xxxxxxxxxxxx_managed_psk
# Enter Wi-Fi password when prompted

connmanctl> quit
```

**Method 2: Using /etc/wpa_supplicant/wpa_supplicant.conf**

```bash
# Edit wpa_supplicant config
sudo nano /etc/wpa_supplicant/wpa_supplicant.conf

# Add network block:
network={
    ssid="YourSSID"
    psk="YourPassword"
    key_mgmt=WPA-PSK
}

# Restart networking
sudo systemctl restart wpa_supplicant
sudo systemctl restart networking
```

**Verify Wi-Fi Connection:**

```bash
# Check Wi-Fi status
ifconfig wlan0

# Expected output:
# wlan0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500
#        inet 192.168.1.100  netmask 255.255.255.0  broadcast 192.168.1.255

# Test internet connectivity
ping -c 3 8.8.8.8
```

### Step 2: Get BBGW IP Address

```bash
# On BBGW via SSH or serial console:
hostname -I

# Example output:
# 192.168.1.100

# Or use ip command:
ip addr show wlan0 | grep 'inet '

# Example output:
# inet 192.168.1.100/24 brd 192.168.1.255 scope global dynamic wlan0
```

**Note BBGW IP address** for later use (e.g., `192.168.1.100`).

### Step 3: Configure Firewall (if enabled)

**Check if firewall is active:**

```bash
sudo ufw status
```

**If active, allow Flask port 5000:**

```bash
sudo ufw allow 5000/tcp
sudo ufw reload
sudo ufw status

# Expected output:
# Status: active
# To                         Action      From
# --                         ------      ----
# 5000/tcp                   ALLOW       Anywhere
```

**If ufw not installed (default on some BBGW images):**

```bash
# Check iptables
sudo iptables -L

# If no blocking rules, firewall is open (default)
```

### Step 4: Verify LAN Connectivity from Laptop

**From laptop:**

```bash
# Ping BBGW
ping -c 3 192.168.1.100

# Expected: 3 successful replies

# Test port 5000 (before Flask starts, should fail)
telnet 192.168.1.100 5000

# Expected: Connection refused (normal if Flask not running)
```

---

## Software Dependencies

### Prerequisites on BBGW

```bash
# Update package lists
sudo apt-get update

# Install Python 3 and pip
sudo apt-get install -y python3 python3-pip

# Verify Python version
python3 --version
# Expected: Python 3.9+ or 3.10+

# Install system dependencies
sudo apt-get install -y python3-dev build-essential
```

### Python Dependencies

```bash
cd ~/bbgw_i2s_source

# Install from requirements.txt
pip3 install -r requirements.txt

# Key packages:
# - flask (web framework)
# - requests (for test script)
# - pyalsaaudio (I2S audio)
# - pyserial (UART communication)
```

**Verify Flask installation:**

```bash
python3 -c "import flask; print(flask.__version__)"

# Expected: 2.x or 3.x
```

---

## Flask Server Deployment

### Step 1: Navigate to Project Directory

```bash
cd ~/bbgw_i2s_source
```

### Step 2: Create Configuration File

```bash
# Copy template if not already created
cp config.yaml.template config.yaml

# Edit config.yaml
nano config.yaml
```

**Verify these settings:**

```yaml
i2s:
  device: "hw:CARD=BBGW-I2S,DEV=0"  # Or "hw:0,0"
  sample_rate: 48000
  channels: 2
  format: "S16_LE"

uart:
  device: "/dev/ttyO4"
  baudrate: 115200
  timeout: 5.0

web:
  host: "0.0.0.0"  # Listen on all interfaces
  port: 5000
  debug: false  # Set to true for development
```

**Important:** Set `host: "0.0.0.0"` to make Flask accessible from LAN.

### Step 3: Start Flask Server

**Foreground mode (for testing):**

```bash
python3 main.py
```

**Expected output:**

```
2026-02-07 10:00:00 - INFO - Starting Flask web server...
 * Serving Flask app 'web.server'
 * Debug mode: off
WARNING: This is a development server. Do not use it in a production deployment.
 * Running on all addresses (0.0.0.0)
 * Running on http://127.0.0.1:5000
 * Running on http://192.168.1.100:5000
Press CTRL+C to quit
```

**Background mode (systemd service, recommended for long-term testing):**

Create systemd service file:

```bash
sudo nano /etc/systemd/system/bbgw-i2s.service
```

Add content:

```ini
[Unit]
Description=BBGW I2S Audio Source Flask Server
After=network.target

[Service]
Type=simple
User=debian
WorkingDirectory=/home/debian/bbgw_i2s_source
ExecStart=/usr/bin/python3 /home/debian/bbgw_i2s_source/main.py
Restart=on-failure
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Enable and start service:

```bash
sudo systemctl daemon-reload
sudo systemctl enable bbgw-i2s.service
sudo systemctl start bbgw-i2s.service

# Check status
sudo systemctl status bbgw-i2s.service

# View logs
sudo journalctl -u bbgw-i2s.service -f
```

### Step 4: Verify Server is Running

**From BBGW (localhost):**

```bash
curl http://localhost:5000

# Expected: HTML content (dashboard page)
```

**From laptop (LAN):**

```bash
curl http://192.168.1.100:5000

# Expected: HTML content (dashboard page)
```

**In browser:**

Navigate to: `http://192.168.1.100:5000`

Expected: Dashboard loads with audio controls.

---

## Running Milestone 3 Test

### Prerequisites Checklist

- [x] BBGW connected to Wi-Fi/LAN (verify: `hostname -I`)
- [x] Flask server running (verify: `curl http://localhost:5000`)
- [x] Laptop connected to same LAN as BBGW
- [x] Python 3.8+ installed on laptop
- [x] `requests` library installed: `pip3 install requests`
- [x] BBGW IP address known (e.g., `192.168.1.100`)

### Test Execution from Laptop

**Download test script to laptop:**

```bash
# Clone repository on laptop (if not already)
git clone https://github.com/yourusername/esp32_btaudio.git
cd esp32_btaudio/bbgw_i2s_source

# Make executable
chmod +x milestone3_web_ui_test.py
```

**Run test:**

```bash
# Test BBGW Flask server
./milestone3_web_ui_test.py --host 192.168.1.100

# With custom port
./milestone3_web_ui_test.py --host 192.168.1.100 --port 5000

# With increased timeout (slower networks)
./milestone3_web_ui_test.py --host 192.168.1.100 --timeout 15
```

### Test Sequence

The test script performs 5 tests:

1. **Server Connectivity** (Test 1)
   - Connects to `http://<bbgw-ip>:5000`
   - Verifies HTTP 200 response
   - Checks Content-Type header

2. **Web UI Pages** (Test 2)
   - Loads dashboard page `/`
   - Verifies HTML content is returned

3. **REST API Endpoints** (Test 3)
   - GET `/api/status` — retrieves system status
   - POST `/api/tone` — generates 440 Hz tone
   - POST `/api/silence` — stops audio playback

4. **Tone Control Latency** (Test 4)
   - Sends 5 tone frequency changes
   - Measures round-trip time for each
   - Validates average latency <200ms

5. **Server-Sent Events** (Test 5)
   - Connects to `/api/stream` (SSE endpoint)
   - Receives 3 status updates
   - Verifies update interval ~500ms (2 Hz)

---

## Manual Browser Testing

### Step 1: Access Dashboard

**URL:** `http://<bbgw-ip>:5000`

Example: `http://192.168.1.100:5000`

**Expected:**
- Dashboard page loads
- Audio control panel visible
- Frequency slider (20 Hz - 20 kHz)
- Amplitude slider (0% - 100%)
- Play/Stop buttons
- Status panel (I2S, Audio, UART)

### Step 2: Test Tone Generation

1. **Set frequency** to 440 Hz (drag slider or type value)
2. **Set amplitude** to 50%
3. **Click "Play Tone"** button

**Expected:**
- Tone starts playing (if I2S and ESP32 connected)
- Status panel updates: "Audio: Playing 440 Hz"
- Button changes to "Stop"

4. **Change frequency** while playing (e.g., to 1000 Hz)

**Expected:**
- Tone frequency changes immediately
- Latency should be <200ms (subjectively instant)

5. **Click "Stop"** button

**Expected:**
- Audio stops
- Status panel updates: "Audio: Stopped"

### Step 3: Test SSE Status Updates

1. **Open browser developer console** (F12)
2. **Navigate to Network tab**
3. **Filter for "stream"**
4. **Refresh page**

**Expected:**
- SSE connection established to `/api/stream`
- Status updates arrive every ~500ms
- Console log shows status JSON objects

### Step 4: Test API Endpoints (Optional)

**Using browser console or curl:**

```javascript
// In browser console on dashboard page:

// Test 1: Get status
fetch('/api/status')
  .then(r => r.json())
  .then(console.log);

// Test 2: Play 1 kHz tone
fetch('/api/tone', {
  method: 'POST',
  headers: {'Content-Type': 'application/json'},
  body: JSON.stringify({freq: 1000, amp: 0.5, mode: 'mono'})
})
  .then(r => r.json())
  .then(console.log);

// Test 3: Silence
fetch('/api/silence', {method: 'POST'})
  .then(r => r.json())
  .then(console.log);
```

---

## Expected Results

### Successful Test Output

```
2026-02-07 10:15:30 - INFO - Milestone 3 Test initialized
2026-02-07 10:15:30 - INFO - Target: http://192.168.1.100:5000
2026-02-07 10:15:30 - INFO - Timeout: 10s
2026-02-07 10:15:30 - INFO - 
2026-02-07 10:15:30 - INFO - ======================================================================
2026-02-07 10:15:30 - INFO - MILESTONE 3: Flask Web UI Validation Test (BBGW)
2026-02-07 10:15:30 - INFO - ======================================================================
2026-02-07 10:15:30 - INFO - 
2026-02-07 10:15:30 - INFO - Test 1: Checking server connectivity...
2026-02-07 10:15:30 - INFO -   Server responded: HTTP 200
2026-02-07 10:15:30 - INFO -   Content-Type: text/html; charset=utf-8
2026-02-07 10:15:30 - INFO - ✓ Server connectivity test PASSED
2026-02-07 10:15:30 - INFO - 
2026-02-07 10:15:30 - INFO - Test 2: Checking web UI pages...
2026-02-07 10:15:31 - INFO -   ✓ Dashboard (/): OK
2026-02-07 10:15:31 - INFO - ✓ Web UI pages test PASSED
2026-02-07 10:15:31 - INFO - 
2026-02-07 10:15:31 - INFO - Test 3: Testing REST API endpoints...
2026-02-07 10:15:31 - INFO -   ✓ GET /api/status: OK (i2s=True)
2026-02-07 10:15:31 - INFO -   ✓ POST /api/tone: OK (440 Hz, 50%)
2026-02-07 10:15:32 - INFO -   ✓ POST /api/silence: OK
2026-02-07 10:15:32 - INFO - ✓ REST API test PASSED
2026-02-07 10:15:32 - INFO - 
2026-02-07 10:15:32 - INFO - Test 4: Testing tone control latency (<200ms)...
2026-02-07 10:15:32 - INFO -   Testing 5 tone changes...
2026-02-07 10:15:32 - INFO -   1000 Hz: 45.2 ms
2026-02-07 10:15:33 - INFO -   440 Hz: 42.8 ms
2026-02-07 10:15:33 - INFO -   2000 Hz: 48.1 ms
2026-02-07 10:15:33 - INFO -   500 Hz: 43.5 ms
2026-02-07 10:15:34 - INFO -   1500 Hz: 46.7 ms
2026-02-07 10:15:34 - INFO - 
2026-02-07 10:15:34 - INFO -   Average latency: 45.3 ms
2026-02-07 10:15:34 - INFO -   Maximum latency: 48.1 ms
2026-02-07 10:15:34 - INFO -   Threshold: 200 ms
2026-02-07 10:15:34 - INFO -   ✓ Latency requirement met (45.3 ms < 200 ms)
2026-02-07 10:15:34 - INFO - ✓ Tone control latency test PASSED
2026-02-07 10:15:34 - INFO - 
2026-02-07 10:15:34 - INFO - Test 5: Testing Server-Sent Events stream...
2026-02-07 10:15:34 - INFO -   Connecting to SSE stream: http://192.168.1.100:5000/api/stream
2026-02-07 10:15:34 - INFO -   SSE stream connected: text/event-stream; charset=utf-8
2026-02-07 10:15:34 - INFO -   Update 1: First update
2026-02-07 10:15:35 - INFO -   Update 2: Interval = 503 ms
2026-02-07 10:15:36 - INFO -   Update 3: Interval = 498 ms
2026-02-07 10:15:36 - INFO - 
2026-02-07 10:15:36 - INFO -   Average interval: 501 ms (expected ~500 ms)
2026-02-07 10:15:36 - INFO -   ✓ SSE update rate OK
2026-02-07 10:15:36 - INFO -   ✓ Received 3 SSE updates
2026-02-07 10:15:36 - INFO - ✓ SSE stream test PASSED
2026-02-07 10:15:36 - INFO - 
2026-02-07 10:15:36 - INFO - ======================================================================
2026-02-07 10:15:36 - INFO - MILESTONE 3 TEST RESULTS
2026-02-07 10:15:36 - INFO - ======================================================================
2026-02-07 10:15:36 - INFO - Tests Run:    5
2026-02-07 10:15:36 - INFO - Tests Passed: 5
2026-02-07 10:15:36 - INFO - Tests Failed: 0
2026-02-07 10:15:36 - INFO - API Calls:    11
2026-02-07 10:15:36 - INFO - API Errors:   0
2026-02-07 10:15:36 - INFO - Avg Latency:  45.3 ms
2026-02-07 10:15:36 - INFO - 
2026-02-07 10:15:36 - INFO - ✓✓✓ ALL MILESTONE 3 SUCCESS CRITERIA MET ✓✓✓
2026-02-07 10:15:36 - INFO - 
2026-02-07 10:15:36 - INFO - Milestone 3 Deliverables Validated:
2026-02-07 10:15:36 - INFO -   ✓ Flask web server accessible on LAN
2026-02-07 10:15:36 - INFO -   ✓ All web UI pages load correctly
2026-02-07 10:15:36 - INFO -   ✓ REST API endpoints function properly
2026-02-07 10:15:36 - INFO -   ✓ Tone control latency <200ms
2026-02-07 10:15:36 - INFO -   ✓ Server-Sent Events stream working
2026-02-07 10:15:36 - INFO - ======================================================================
```

---

## Troubleshooting

### Issue 1: Cannot connect to Flask server from laptop

**Symptoms:**
```
Connection refused: http://192.168.1.100:5000
```

**Causes:**
- Flask server not running on BBGW
- BBGW firewall blocking port 5000
- Laptop and BBGW on different networks
- Flask listening on localhost only

**Solutions:**

1. **Verify Flask is running on BBGW:**
   ```bash
   # On BBGW:
   curl http://localhost:5000
   
   # If fails, start Flask:
   cd ~/bbgw_i2s_source
   python3 main.py
   ```

2. **Check Flask host binding:**
   ```bash
   # In config.yaml or main.py, ensure:
   host: "0.0.0.0"  # NOT "127.0.0.1" or "localhost"
   ```

3. **Verify firewall:**
   ```bash
   sudo ufw allow 5000/tcp
   sudo ufw reload
   ```

4. **Check same network:**
   ```bash
   # On laptop:
   ping 192.168.1.100
   
   # On BBGW:
   hostname -I
   ```

### Issue 2: Web page loads but API calls fail (404)

**Symptoms:**
```
GET /api/status: HTTP 404
```

**Causes:**
- API routes not registered
- Flask app not fully initialized

**Solutions:**

1. **Check Flask app structure:**
   ```bash
   # On BBGW:
   grep -r "api/status" ~/bbgw_i2s_source/web/
   
   # Should find route definitions
   ```

2. **Restart Flask with debug:**
   ```bash
   # Edit config.yaml:
   web:
     debug: true
   
   # Restart Flask
   python3 main.py
   
   # Check logs for route registration
   ```

3. **Verify API blueprint:**
   ```bash
   # Check web/server.py or similar
   # Should have: app.register_blueprint(api_bp, url_prefix='/api')
   ```

### Issue 3: Tone control latency >200ms

**Symptoms:**
```
Average latency: 350 ms
✗ Latency requirement NOT met
```

**Causes:**
- Slow network (Wi-Fi congestion)
- BBGW CPU overloaded
- I2S driver not optimized

**Solutions:**

1. **Test network latency:**
   ```bash
   # On laptop:
   ping -c 10 192.168.1.100
   
   # Check avg latency (should be <10ms)
   ```

2. **Check BBGW CPU usage:**
   ```bash
   # On BBGW:
   top
   
   # If >80% CPU, reduce background processes
   ```

3. **Use wired Ethernet instead of Wi-Fi:**
   ```bash
   # Connect USB Ethernet adapter to BBGW
   # Retest latency
   ```

4. **Optimize Flask:**
   ```bash
   # Disable debug mode in config.yaml:
   web:
     debug: false
   ```

### Issue 4: SSE stream disconnects or no updates

**Symptoms:**
```
SSE stream: Timeout waiting for updates
```

**Causes:**
- SSE endpoint not implemented
- Flask app not sending events
- Proxy/firewall blocking SSE

**Solutions:**

1. **Test SSE endpoint manually:**
   ```bash
   # On laptop:
   curl -N http://192.168.1.100:5000/api/stream
   
   # Should stream events continuously
   # Press Ctrl+C to stop
   ```

2. **Check Flask SSE implementation:**
   ```bash
   # On BBGW:
   grep -r "text/event-stream" ~/bbgw_i2s_source/web/
   
   # Should find SSE response headers
   ```

3. **Increase timeout:**
   ```bash
   ./milestone3_web_ui_test.py --host 192.168.1.100 --timeout 20
   ```

### Issue 5: Dashboard loads but controls don't work

**Symptoms:**
- Dashboard visible
- Sliders/buttons don't respond
- JavaScript errors in console

**Causes:**
- JavaScript file not loaded
- API endpoints failing
- CORS issues (if testing from different origin)

**Solutions:**

1. **Check browser console (F12):**
   ```
   Look for JavaScript errors or failed API calls
   ```

2. **Verify static files:**
   ```bash
   # On BBGW:
   ls ~/bbgw_i2s_source/web/static/
   
   # Should see: js/, css/, etc.
   ```

3. **Check Flask static routes:**
   ```bash
   curl http://192.168.1.100:5000/static/js/app.js
   
   # Should return JavaScript content
   ```

### Issue 6: "requests" module not found

**Symptoms:**
```
ModuleNotFoundError: No module named 'requests'
```

**Solution:**

```bash
# On laptop (where test script runs):
pip3 install requests

# Or use pip:
pip install requests
```

---

## Success Validation

### Checklist for Milestone 3 Completion

Mark each item when verified:

- [ ] **Network Setup:**
  - [ ] BBGW connected to Wi-Fi or Ethernet (`hostname -I` shows IP)
  - [ ] Laptop on same LAN as BBGW
  - [ ] Firewall allows port 5000 (if enabled)
  - [ ] Ping successful from laptop to BBGW

- [ ] **Flask Server:**
  - [ ] Flask server running on BBGW
  - [ ] config.yaml configured with `host: "0.0.0.0"`
  - [ ] Server accessible on localhost: `curl http://localhost:5000`
  - [ ] Server accessible from LAN: `curl http://<bbgw-ip>:5000`

- [ ] **Web UI:**
  - [ ] Dashboard loads in browser
  - [ ] Controls visible (sliders, buttons)
  - [ ] Status panel displays I2S/Audio/UART info
  - [ ] No JavaScript errors in console

- [ ] **Milestone 3 Test Script:**
  - [ ] Test script executable on laptop
  - [ ] `requests` library installed
  - [ ] Test 1 (Server connectivity): PASSED
  - [ ] Test 2 (Web UI pages): PASSED
  - [ ] Test 3 (REST API): PASSED
  - [ ] Test 4 (Latency <200ms): PASSED
  - [ ] Test 5 (SSE stream): PASSED

- [ ] **Manual Testing:**
  - [ ] Play tone from browser (slider changes frequency)
  - [ ] Latency feels instant (<200ms subjectively)
  - [ ] Status updates in real-time
  - [ ] All pages/tabs accessible

### Final Acceptance Criteria

**Milestone 3 is considered COMPLETE when:**

1. ✅ **Flask server accessible on LAN** from laptop/smartphone browser
2. ✅ **All 5 automated tests pass** (`milestone3_web_ui_test.py` exit code 0)
3. ✅ **Average tone control latency <200ms** (preferably <100ms)
4. ✅ **SSE updates at ~2 Hz** (500ms intervals ±20%)
5. ✅ **Web UI responsive** (controls work, pages load, no errors)

---

## Next Steps

After completing Milestone 3:

1. **Integration Testing:**
   - Combine I2S, UART, and Web UI (Milestones 1+2+3)
   - End-to-end test: control audio via web UI → UART → ESP32 → I2S playback

2. **Performance Optimization:**
   - Profile Flask app for bottlenecks
   - Optimize I2S buffer management
   - Tune SSE update rate

3. **Production Deployment:**
   - Set up systemd service (persistent Flask server)
   - Configure nginx reverse proxy (optional)
   - Enable HTTPS (optional, for secure LAN access)

4. **Advanced Features:**
   - Add frequency sweep control to web UI
   - Implement Bluetooth pairing via UI
   - Add audio visualization (FFT spectrum)

---

## References

### BeagleBone Documentation
- [BBGW Wi-Fi Setup Guide](https://beagleboard.org/p/products/beaglebone-green-wireless)
- [Debian Networking on BeagleBone](https://elinux.org/Beagleboard:BeagleBoneBlack_Debian#Wifi)

### Flask Documentation
- [Flask Quickstart](https://flask.palletsprojects.com/en/latest/quickstart/)
- [Flask Deployment on Linux](https://flask.palletsprojects.com/en/latest/deploying/)
- [Server-Sent Events (SSE) in Flask](https://flask.palletsprojects.com/en/latest/patterns/streaming/)

### Networking
- [connman Command-Line Client](https://wiki.archlinux.org/title/ConnMan)
- [systemd Networking](https://www.freedesktop.org/software/systemd/man/systemd.network.html)

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-07  
**Author:** BBGW I2S Audio Source Project  
**License:** MIT
