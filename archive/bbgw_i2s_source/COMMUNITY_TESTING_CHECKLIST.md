# Community Testing Checklist — BBGW I2S Source v1.0.0-bbgw

## Quick Start for Community Testers

Thank you for helping test the BeagleBone Green Wireless I2S Source project! This checklist will guide you through the essential tests.

**Estimated Time:** 1-2 hours  
**Required Hardware:** BeagleBone Green Wireless (AM335x)  
**Optional Hardware:** ESP32 with esp_bt_audio_source firmware, Bluetooth speaker

---

## Pre-Test Setup

### 1. Hardware Verification ✓
- [ ] BeagleBone Green Wireless confirmed (not BBB or other variant)
- [ ] Debian 11+ installed on microSD card
- [ ] Network connectivity available (Wi-Fi or USB Ethernet)
- [ ] SSH or serial console access configured

### 2. Record Your Environment 📝
```
BBGW Hardware Revision: ___________ (check board)
Debian Version: ___________ (lsb_release -a)
Kernel Version: ___________ (uname -r)
Date: ___________
Tester: ___________
```

---

## Essential Tests (30-60 minutes)

### Test 1: Installation ⚙️

**Goal:** Verify setup_bbgw.sh works on your system

```bash
# Clone repository
cd /home/debian
git clone --branch v1.0.0-bbgw https://github.com/ekkus93/esp32_btaudio.git
cd esp32_btaudio/bbgw_i2s_source

# Run automated setup
bash setup_bbgw.sh
```

**Checklist:**
- [ ] Setup completes all 10 steps without errors
- [ ] Color-coded output is readable
- [ ] Manual steps (Device Tree) are clearly explained
- [ ] Summary shows next steps

**If errors occur:** Note the step number and error message

**After reboot:**
```bash
# Verify UART4
ls -l /dev/ttyO4
# Expected: crw-rw---- ... dialout ...

# Verify ALSA I2S device
aplay -l
# Expected: BBGW-I2S device listed
```

- [ ] UART4 device exists: /dev/ttyO4
- [ ] ALSA device exists: BBGW-I2S

**Installation Rating:** ⭐⭐⭐⭐⭐ (1=Failed, 5=Perfect)

**Issues Found:**
```
[Describe any problems]
```

---

### Test 2: Application Launch 🚀

**Goal:** Verify application starts without errors

```bash
cd /home/debian/esp32_btaudio/bbgw_i2s_source
source venv/bin/activate
python3 main.py
```

**Checklist:**
- [ ] Application starts within 10 seconds
- [ ] No Python errors in console
- [ ] Log messages indicate successful I2S and UART initialization
- [ ] Web server starts on port 5000

**Application Startup Rating:** ⭐⭐⭐⭐⭐

**Issues Found:**
```
[Describe any problems]
```

---

### Test 3: Web UI Access 🌐

**Goal:** Verify web interface loads and is usable

**Access web UI:**
```
http://beaglebone.local:5000
# Or use IP address: http://192.168.x.x:5000
```

**Checklist:**
- [ ] Page loads in <5 seconds
- [ ] All UI elements visible (tone controls, source selection, BT controls)
- [ ] No JavaScript errors in browser console (F12 → Console)
- [ ] Status updates appear (watch for changing values)

**Test Controls:**
- [ ] Frequency slider moves smoothly
- [ ] Amplitude slider moves smoothly
- [ ] "Generate Tone" button responds immediately
- [ ] Source selection dropdown works

**Web UI Rating:** ⭐⭐⭐⭐⭐

**Issues Found:**
```
[Describe any problems]
```

---

### Test 4: Audio Generation 🎵

**Goal:** Verify I2S audio output (basic test)

**Generate tone via web UI:**
1. Set frequency: 1000 Hz
2. Set amplitude: 0.5
3. Click "Generate Tone"

**Checklist:**
- [ ] Tone starts immediately (no delay >2 seconds)
- [ ] Application logs show audio generation
- [ ] No error messages in logs
- [ ] Can change frequency and hear/see change in logs
- [ ] Can stop tone (select "Silence")

**If you have oscilloscope or logic analyzer:**
- [ ] I2S BCLK present on P9.31 (~3 MHz for 48 kHz)
- [ ] I2S WS present on P9.29 (48 kHz square wave)
- [ ] I2S DOUT present on P9.28 (data stream)

**Audio Generation Rating:** ⭐⭐⭐⭐⭐

**Issues Found:**
```
[Describe any problems]
```

---

### Test 5: UART Communication 📡

**Goal:** Verify UART control interface (basic test)

**Option A: Using minicom**
```bash
# In separate terminal
sudo apt install minicom
minicom -D /dev/ttyO4 -b 115200

# Send commands:
STATUS
VOLUME 50
```

**Option B: Using Python**
```python
import serial
ser = serial.Serial('/dev/ttyO4', 115200, timeout=5)
ser.write(b'STATUS\n')
response = ser.readline()
print(response)
# Expected: OK|STATUS|{...}
```

**Checklist:**
- [ ] STATUS command returns response
- [ ] Response format is OK|STATUS|...
- [ ] VOLUME command changes volume (check web UI)
- [ ] No garbled characters

**UART Communication Rating:** ⭐⭐⭐⭐⭐

**Issues Found:**
```
[Describe any problems]
```

---

## Extended Tests (Optional, 30-60 minutes)

### Test 6: Different Audio Sources 🎼

**Try each audio source:**

- [ ] **Tone Generator:** 440 Hz, 1000 Hz, 10000 Hz all work
- [ ] **Dual Tone:** Activates successfully
- [ ] **Frequency Sweep:** Runs for ~10 seconds, 20 Hz → 20 kHz
- [ ] **Silence:** Audio stops

**If you have WAV files:**
```bash
# Copy 44.1 kHz WAV to audio directory
cp /path/to/test.wav /home/debian/audio/

# Select from web UI dropdown
```

- [ ] WAV file appears in dropdown
- [ ] WAV plays successfully
- [ ] Auto-resampling to 48 kHz works

**Audio Sources Rating:** ⭐⭐⭐⭐⭐

---

### Test 7: Stability Test 🕐

**Goal:** Verify system stability over time

**Run for 30 minutes:**
1. Generate 1000 Hz tone
2. Leave running
3. Monitor CPU and memory

```bash
# In separate terminal
watch -n 5 'ps aux | grep main.py'
# Check CPU% and MEM%
```

**Checklist:**
- [ ] Application runs for 30+ minutes without crashing
- [ ] CPU usage stable (15-30%)
- [ ] Memory usage stable (<200 MB)
- [ ] No buffer underruns in logs
- [ ] Web UI remains responsive

**Stability Rating:** ⭐⭐⭐⭐⭐

---

### Test 8: ESP32 Integration (If Available) 🔊

**Goal:** Test full Bluetooth audio pipeline

**Prerequisites:**
- ESP32 with esp_bt_audio_source firmware
- UART connection: BBGW UART4 ↔ ESP32 UART
- I2S connection: BBGW I2S ↔ ESP32 I2S
- Bluetooth speaker paired with ESP32

**Checklist:**
- [ ] UART commands reach ESP32 (check ESP32 logs)
- [ ] Bluetooth scan works via web UI
- [ ] Can connect to Bluetooth speaker
- [ ] Audio plays through Bluetooth speaker
- [ ] Audio quality is good (no distortion/dropouts)
- [ ] Volume control works end-to-end

**Integration Rating:** ⭐⭐⭐⭐⭐

---

## Documentation Review (Optional, 30 minutes)

### Rate Documentation Clarity

**README.md:**
- [ ] Clear overview of project
- [ ] Quick start instructions work
- **Rating:** ⭐⭐⭐⭐⭐

**HARDWARE_SETUP_BBGW.md:**
- [ ] Pin connections clear
- [ ] Device Tree instructions work
- **Rating:** ⭐⭐⭐⭐⭐

**SOFTWARE_SETUP_BBGW.md:**
- [ ] Dependencies correct
- [ ] Setup steps complete
- **Rating:** ⭐⭐⭐⭐⭐

**TROUBLESHOOTING_BBGW.md:**
- [ ] Issues organized well
- [ ] Solutions effective
- **Rating:** ⭐⭐⭐⭐⭐

**What's Missing?**
```
[Any documentation gaps or unclear sections]
```

**Suggestions:**
```
[How documentation could be improved]
```

---

## Issue Reporting

### If You Found Issues

**Please report on GitHub:**
1. Go to: https://github.com/ekkus93/esp32_btaudio/issues
2. Click "New Issue"
3. Use template below

```markdown
## UAT Issue — [Brief Title]

**Tester:** [Your Name]
**Date:** [YYYY-MM-DD]
**BBGW Revision:** [e.g., A5C]
**Debian:** [version]
**Kernel:** [version]

**Issue Type:** [Bug | Documentation | Enhancement]
**Severity:** [Low | Medium | High | Critical]
**Test Phase:** [Installation | Audio | UART | Web UI | Integration]

### Description
[What went wrong?]

### Steps to Reproduce
1. [Step 1]
2. [Step 2]

### Expected
[What should happen]

### Actual
[What actually happened]

### Logs
```
[Paste relevant logs]
```

### Workaround
[If you found a workaround]
```

---

## Final Summary

### Overall Experience

**Time Spent:** _____ hours

**Overall Rating:** ⭐⭐⭐⭐⭐ (1=Unusable, 5=Excellent)

**Would you recommend this project?** [ ] Yes [ ] No [ ] Maybe

**Best Feature:**
```
[What impressed you most?]
```

**Biggest Problem:**
```
[What needs the most work?]
```

**Additional Comments:**
```
[Any other feedback]
```

---

## Submit Your Feedback

**Option 1: GitHub Issues**
- Open issue at: https://github.com/ekkus93/esp32_btaudio/issues
- Title: "UAT Feedback — [Your Name]"
- Paste completed checklist

**Option 2: Email**
- Send to: [maintainer email]
- Subject: "BBGW I2S Source v1.0.0-bbgw UAT Feedback"
- Attach completed checklist

**Option 3: BeagleBoard Forum**
- Post at: https://forum.beagleboard.org/
- Tag: BBGW, I2S, ESP32
- Link to this project

---

## Thank You! 🙏

Your feedback is invaluable for improving this project. Community testing helps ensure the BBGW I2S Source works reliably across different hardware revisions, Debian versions, and use cases.

**Contributors will be acknowledged in:**
- Updated RELEASE_NOTES.md
- Project README.md
- GitHub contributors list

**Questions?**
- GitHub Discussions: https://github.com/ekkus93/esp32_btaudio/discussions
- BeagleBoard Forum: https://forum.beagleboard.org/

---

**Checklist Version:** 1.0.0-bbgw  
**Last Updated:** 2026-02-07  
**Status:** Active — Accepting UAT feedback
