# User Acceptance Testing Guide

## Overview

This guide provides a comprehensive framework for user acceptance testing (UAT) of the BeagleBone Green Wireless I2S Source project. It includes test scenarios, acceptance criteria, feedback templates, and validation procedures for community testing.

**Target Version:** v1.0.0-bbgw  
**Last Updated:** 2026-02-07

---

## Purpose of User Acceptance Testing

User Acceptance Testing validates that the BBGW I2S Source:
1. **Functions correctly** on real BeagleBone Green Wireless hardware
2. **Meets user expectations** for ease of setup and use
3. **Documentation is clear** and complete for new users
4. **Troubleshooting guides** effectively resolve common issues
5. **Performance is acceptable** for typical use cases

UAT complements unit/integration/performance tests by focusing on **real-world user experience**.

---

## Test Environment Requirements

### Hardware Requirements
- **Platform:** BeagleBone Green Wireless (AM335x)
- **Power:** 5V 2A power supply (barrel jack or USB)
- **Storage:** 4 GB+ microSD card (Class 10 recommended)
- **Network:** Wi-Fi access (2.4 GHz) or USB Ethernet gadget mode
- **Optional:** ESP32 with esp_bt_audio_source firmware for full integration test

### Software Requirements
- **OS:** Debian 11.x or later (clean install recommended)
- **Kernel:** 5.10+ with Device Tree support
- **Access:** SSH or serial console access to BBGW
- **Tools:** Web browser for accessing web UI

### Test Environment Setup
```bash
# 1. Flash latest Debian image to microSD card
# Download from: https://beagleboard.org/latest-images

# 2. Boot BBGW with fresh microSD card
# 3. Connect via SSH
ssh debian@beaglebone.local
# Default password: temppwd

# 4. Update system (optional but recommended)
sudo apt update && sudo apt upgrade -y

# 5. Verify kernel version
uname -r
# Expected: 5.10.x or later
```

---

## Test Scenarios

### Scenario 1: Fresh Installation (Critical Path)

**Objective:** Verify automated setup script works on clean system

**Prerequisites:**
- Fresh Debian 11+ installation
- Internet connectivity
- No previous BBGW I2S Source installation

**Test Steps:**
1. Clone repository:
   ```bash
   cd /home/debian
   git clone --branch v1.0.0-bbgw https://github.com/ekkus93/esp32_btaudio.git
   cd esp32_btaudio/bbgw_i2s_source
   ```

2. Run automated setup:
   ```bash
   bash setup_bbgw.sh
   ```

3. Observe setup process:
   - [ ] All 10 steps execute without errors
   - [ ] Color-coded output is clear and helpful
   - [ ] Manual steps are clearly documented
   - [ ] Summary provides next steps

4. Follow manual Device Tree overlay compilation:
   - [ ] UART4 overlay instructions clear
   - [ ] McASP I2S overlay instructions clear
   - [ ] Compilation succeeds without errors

5. Reboot system:
   ```bash
   sudo reboot
   ```

6. Verify Device Tree overlays loaded:
   ```bash
   ls -l /dev/ttyO4
   # Expected: crw-rw---- 1 root dialout ...
   
   aplay -l
   # Expected: BBGW-I2S device listed
   ```

7. Run application:
   ```bash
   cd /home/debian/esp32_btaudio/bbgw_i2s_source
   source venv/bin/activate
   python3 main.py
   ```

8. Access web UI:
   ```
   http://beaglebone.local:5000
   ```

**Acceptance Criteria:**
- [ ] Setup completes without errors
- [ ] UART4 device available (/dev/ttyO4)
- [ ] ALSA I2S device available
- [ ] Application starts without errors
- [ ] Web UI accessible and responsive
- [ ] Setup time: <30 minutes (excluding downloads)

**Common Issues to Test:**
- Missing dependencies (setup should install)
- Permission issues (setup should add user to dialout)
- Existing configurations (setup should be idempotent)

---

### Scenario 2: I2S Audio Output

**Objective:** Verify I2S audio generation and output quality

**Prerequisites:**
- Scenario 1 complete (application running)
- Logic analyzer or oscilloscope (optional, for advanced testing)
- I2S-compatible device connected to P9 header (optional)

**Test Steps:**
1. Generate 1 kHz tone via web UI:
   - Navigate to Tone Generator section
   - Set frequency: 1000 Hz
   - Set amplitude: 0.5
   - Click "Generate Tone"
   - [ ] Tone starts immediately
   - [ ] No audio dropouts or glitches

2. Test frequency range:
   - [ ] 20 Hz: Generates successfully
   - [ ] 440 Hz: Generates successfully
   - [ ] 1000 Hz: Generates successfully
   - [ ] 10000 Hz: Generates successfully
   - [ ] 20000 Hz: Generates successfully

3. Test amplitude range:
   - [ ] 0.1: Low volume, no distortion
   - [ ] 0.5: Medium volume, no distortion
   - [ ] 0.9: High volume, no distortion
   - [ ] 1.0: Maximum volume, no distortion

4. Test dual-tone mode:
   - Select "Dual Tone" source
   - [ ] 1 kHz on left channel
   - [ ] 440 Hz on right channel
   - [ ] Channels correctly separated

5. Test frequency sweep:
   - Select "Frequency Sweep" source
   - [ ] Sweep starts at 20 Hz
   - [ ] Sweep ends at 20 kHz
   - [ ] Sweep duration ~10 seconds
   - [ ] Logarithmic progression audible

6. Test WAV playback:
   - Place 44.1 kHz WAV file in `/home/debian/audio/`
   - Select WAV file from web UI
   - [ ] File loads successfully
   - [ ] Auto-resamples to 48 kHz
   - [ ] Playback smooth, no clicks/pops
   - [ ] Loops continuously

**Acceptance Criteria:**
- [ ] All tone frequencies generate correctly
- [ ] No audio distortion at any amplitude
- [ ] Dual-tone mode has correct channel separation
- [ ] Frequency sweep is smooth
- [ ] WAV playback is glitch-free
- [ ] I2S signals present on P9.31 (BCLK), P9.29 (WS), P9.28 (DOUT)

**Performance Metrics:**
- [ ] CPU usage: 15-25% during 48 kHz stereo streaming
- [ ] Buffer underruns: <5 per hour (check with `journalctl -u bbgw_i2s_source -f`)
- [ ] Audio latency: 21-23 ms (default buffer size)

---

### Scenario 3: UART Control Interface

**Objective:** Verify UART command/response protocol

**Prerequisites:**
- Scenario 1 complete (application running)
- UART4 device available (/dev/ttyO4)
- Serial terminal or ESP32 connected to UART4

**Test Steps:**
1. Connect to UART4 via serial terminal:
   ```bash
   # Install minicom (if not already installed)
   sudo apt install minicom
   
   # Connect to UART4
   minicom -D /dev/ttyO4 -b 115200
   ```

2. Test STATUS command:
   ```
   STATUS
   # Expected: OK|STATUS|<JSON status>
   ```
   - [ ] Response format correct
   - [ ] JSON parses successfully
   - [ ] Contains: audio_source, bt_connected, bt_address, volume

3. Test VOLUME command:
   ```
   VOLUME 50
   # Expected: OK|VOLUME|50
   ```
   - [ ] Volume changes in application
   - [ ] Response confirms new volume

4. Test SCAN command:
   ```
   SCAN
   # Expected: OK|SCAN|<list of devices>
   ```
   - [ ] Returns list of Bluetooth devices (if ESP32 connected)
   - [ ] Timeout handled gracefully if no ESP32

5. Test CONNECT command:
   ```
   CONNECT AA:BB:CC:DD:EE:FF
   # Expected: OK|CONNECT (if ESP32 responds)
   # Or: ERR|CONNECT|timeout (if no ESP32)
   ```
   - [ ] Connection attempt occurs
   - [ ] Response indicates success or failure

6. Test error handling:
   ```
   INVALID_COMMAND
   # Expected: ERR|INVALID_COMMAND|Unknown command
   ```
   - [ ] Unknown commands return error
   - [ ] Error message is descriptive

7. Test event notifications:
   - Connect/disconnect ESP32 (if available)
   - [ ] BT_CONNECTED event received when ESP32 connects
   - [ ] BT_DISCONNECTED event received when ESP32 disconnects

**Acceptance Criteria:**
- [ ] All valid commands execute successfully
- [ ] Response format matches protocol (OK|COMMAND|result)
- [ ] Error responses are clear and helpful
- [ ] UART latency <50 ms round-trip
- [ ] No garbled data or framing errors
- [ ] Event notifications arrive within 2 seconds

---

### Scenario 4: Web User Interface

**Objective:** Verify web UI functionality and responsiveness

**Prerequisites:**
- Scenario 1 complete (application running)
- Web browser (Chrome, Firefox, Safari, Edge)

**Test Steps:**
1. Access web UI:
   ```
   http://beaglebone.local:5000
   ```
   - [ ] Page loads in <3 seconds
   - [ ] All elements render correctly
   - [ ] No console errors

2. Test tone generator controls:
   - [ ] Frequency slider: Smooth adjustment (20 Hz - 20 kHz)
   - [ ] Amplitude slider: Smooth adjustment (0.0 - 1.0)
   - [ ] "Generate Tone" button: Immediate response
   - [ ] Frequency/amplitude values update correctly

3. Test source selection:
   - [ ] Tone option: Activates tone generator
   - [ ] Dual Tone option: Activates dual-tone mode
   - [ ] Sweep option: Starts frequency sweep
   - [ ] Silence option: Stops all audio
   - [ ] WAV dropdown: Lists available WAV files

4. Test Server-Sent Events (SSE):
   - Observe status updates in browser console or Network tab
   - [ ] Status updates arrive every 500 ms (2 Hz)
   - [ ] Updates include: audio_source, volume, BT status
   - [ ] Connection stays alive for >5 minutes

5. Test Bluetooth controls (if ESP32 connected):
   - [ ] "Scan" button: Triggers Bluetooth scan
   - [ ] Device list: Updates with discovered devices
   - [ ] "Connect" button: Initiates connection
   - [ ] "Disconnect" button: Terminates connection
   - [ ] Status indicator: Shows connection state

6. Test responsive design:
   - Desktop (1920x1080):
     - [ ] Layout is clear and spacious
     - [ ] All controls accessible
   - Tablet (1024x768):
     - [ ] Layout adapts appropriately
     - [ ] Controls remain usable
   - Mobile (375x667):
     - [ ] Layout stacks vertically
     - [ ] Touch targets are adequate

**Acceptance Criteria:**
- [ ] Web UI loads quickly (<3 seconds)
- [ ] All controls functional and responsive
- [ ] SSE updates work reliably
- [ ] No JavaScript errors in console
- [ ] Responsive design works on all screen sizes
- [ ] UI remains responsive during audio generation

---

### Scenario 5: Documentation Clarity

**Objective:** Verify documentation is complete, accurate, and helpful

**Test Steps:**

1. **HARDWARE_SETUP_BBGW.md Review:**
   - [ ] Prerequisites clearly listed
   - [ ] Pin connections documented with diagrams
   - [ ] Device Tree overlay instructions step-by-step
   - [ ] Verification steps provided
   - [ ] Able to follow without prior Device Tree knowledge

2. **SOFTWARE_SETUP_BBGW.md Review:**
   - [ ] System dependencies listed completely
   - [ ] Python environment setup clear
   - [ ] Configuration steps logical
   - [ ] Example config.yaml provided
   - [ ] Able to complete setup without errors

3. **TROUBLESHOOTING_BBGW.md Review:**
   - [ ] Issues organized by category
   - [ ] Each issue has clear symptoms
   - [ ] Diagnostic steps are actionable
   - [ ] Solutions are effective
   - [ ] Quick diagnostics section helpful

4. **BBGW_DEVICE_TREE_GUIDE.md Review:**
   - [ ] Device Tree concepts explained clearly
   - [ ] Pin muxing documented thoroughly
   - [ ] Compilation steps work as written
   - [ ] Examples are accurate
   - [ ] Advanced topics (conflicts, debugging) covered

5. **PERFORMANCE_OPTIMIZATION.md Review:**
   - [ ] Baseline performance documented
   - [ ] Optimization strategies clear
   - [ ] Trade-offs explained
   - [ ] Tuning examples provided
   - [ ] Production deployment guidance complete

6. **RELEASE_NOTES.md Review:**
   - [ ] Features listed comprehensively
   - [ ] Quick Start instructions work
   - [ ] Known issues documented
   - [ ] Migration guide helpful
   - [ ] Project statistics accurate

7. **VERSION_TAGGING_GUIDE.md Review:**
   - [ ] Version numbering scheme clear
   - [ ] Git tagging workflow correct
   - [ ] GitHub release steps work
   - [ ] Release checklist complete
   - [ ] Hotfix/rollback procedures clear

8. **README.md Review:**
   - [ ] Project overview clear
   - [ ] Quick start section concise
   - [ ] Links to detailed docs work
   - [ ] Example commands correct
   - [ ] Support section helpful

**Acceptance Criteria:**
- [ ] Can complete full setup following only documentation
- [ ] Troubleshooting guide resolves actual issues encountered
- [ ] No missing steps or unclear instructions
- [ ] Technical accuracy: 100%
- [ ] Links and references all valid
- [ ] Code examples execute successfully

**Documentation Feedback Template:**
```markdown
## Documentation Issue Report

**Document:** [filename]
**Section:** [section or line number]
**Issue Type:** [Missing Info | Unclear | Incorrect | Typo]

**Description:**
[What is unclear, missing, or incorrect?]

**Suggested Improvement:**
[How could this be improved?]

**Impact:** [Low | Medium | High]
```

---

### Scenario 6: End-to-End Integration

**Objective:** Verify complete system with ESP32 Bluetooth audio

**Prerequisites:**
- BBGW I2S Source running
- ESP32 with esp_bt_audio_source firmware
- Bluetooth speaker or headphones
- UART connection between BBGW and ESP32

**Test Steps:**
1. Connect hardware:
   - BBGW UART4 → ESP32 UART (P9.11/P9.13 → ESP32 RX/TX)
   - BBGW I2S → ESP32 I2S (P9.31/P9.29/P9.28 → ESP32 I2S pins)
   - ESP32 powered and running bt_audio_source firmware

2. Start BBGW application:
   ```bash
   cd /home/debian/esp32_btaudio/bbgw_i2s_source
   source venv/bin/activate
   python3 main.py
   ```

3. Verify UART communication:
   - Check application logs for ESP32 connection
   - Send STATUS command via web UI
   - [ ] ESP32 responds with status

4. Pair Bluetooth speaker:
   - Use web UI to scan for devices
   - Select Bluetooth speaker from list
   - Click "Connect"
   - [ ] Pairing successful
   - [ ] Speaker shows as connected

5. Stream audio:
   - Generate 1 kHz tone via web UI
   - [ ] Tone plays through Bluetooth speaker
   - [ ] Audio quality is good (no distortion/dropouts)
   - [ ] Volume control works

6. Test source switching:
   - Switch to Dual Tone
   - [ ] Audio changes immediately
   - [ ] Left/right channels correct
   - Switch to Frequency Sweep
   - [ ] Sweep plays smoothly
   - Switch to WAV file
   - [ ] WAV plays correctly

7. Test Bluetooth disconnect/reconnect:
   - Click "Disconnect" in web UI
   - [ ] Speaker disconnects
   - [ ] Audio stops
   - Click "Connect" again
   - [ ] Reconnection successful
   - [ ] Audio resumes

8. Stress test:
   - Run for 1 hour continuous playback
   - [ ] No audio dropouts
   - [ ] No memory leaks (check with `free -m`)
   - [ ] No excessive CPU usage
   - [ ] No buffer underruns

**Acceptance Criteria:**
- [ ] End-to-end audio path works (BBGW → I2S → ESP32 → Bluetooth → Speaker)
- [ ] UART control fully functional
- [ ] Web UI controls affect Bluetooth audio
- [ ] System stable for >1 hour
- [ ] Audio quality meets expectations
- [ ] Latency acceptable (<100 ms total pipeline)

---

## Feedback Collection

### User Feedback Form

```markdown
# BBGW I2S Source v1.0.0-bbgw — User Feedback

**Tester Name:** [Your Name]  
**Date:** [YYYY-MM-DD]  
**BBGW Hardware Revision:** [e.g., A5C]  
**Debian Version:** [e.g., 11.6]  
**Kernel Version:** [e.g., 5.10.168-ti-r72]

---

## Installation Experience

**Setup Method Used:** [Automated (setup_bbgw.sh) | Manual]

**Setup Time:** [minutes]

**Issues Encountered:**
- [ ] None
- [ ] Dependency installation failed
- [ ] Device Tree compilation errors
- [ ] Permission issues
- [ ] Other: _______________

**Setup Difficulty:** [1=Very Easy | 2=Easy | 3=Moderate | 4=Hard | 5=Very Hard]

**Comments:**
[Any issues or suggestions for setup process]

---

## Functionality Testing

**Features Tested:** (check all that apply)
- [ ] Tone Generator
- [ ] Dual Tone
- [ ] Frequency Sweep
- [ ] WAV Playback
- [ ] UART Control
- [ ] Web UI
- [ ] Bluetooth Integration (with ESP32)

**Issues Found:**
1. [Description]
   - **Severity:** [Low | Medium | High | Critical]
   - **Reproducible:** [Yes | No]
   - **Steps to Reproduce:** [...]

**Performance:**
- **CPU Usage:** [%]
- **Memory Usage:** [MB]
- **Audio Quality:** [1=Poor | 2=Fair | 3=Good | 4=Very Good | 5=Excellent]
- **Latency:** [Acceptable | Noticeable | Unacceptable]

---

## Documentation Feedback

**Documents Reviewed:** (check all that apply)
- [ ] README.md
- [ ] HARDWARE_SETUP_BBGW.md
- [ ] SOFTWARE_SETUP_BBGW.md
- [ ] TROUBLESHOOTING_BBGW.md
- [ ] BBGW_DEVICE_TREE_GUIDE.md
- [ ] PERFORMANCE_OPTIMIZATION.md
- [ ] RELEASE_NOTES.md

**Documentation Quality:** [1=Poor | 2=Fair | 3=Good | 4=Very Good | 5=Excellent]

**Missing Information:**
[What's missing or needs more detail?]

**Unclear Sections:**
[Which sections were confusing?]

**Suggestions:**
[How can documentation be improved?]

---

## Overall Experience

**Overall Satisfaction:** [1=Very Unsatisfied | 2=Unsatisfied | 3=Neutral | 4=Satisfied | 5=Very Satisfied]

**Would you recommend this project?** [Yes | No | Maybe]

**Most Positive Aspect:**
[What did you like most?]

**Most Negative Aspect:**
[What needs the most improvement?]

**Additional Comments:**
[Any other feedback]

---

**Submit Feedback:**
- GitHub Issues: https://github.com/ekkus93/esp32_btaudio/issues
- Email: [project maintainer email]
- Forum: [BeagleBoard.org forums link]
```

---

## Issue Tracking Template

For issues found during UAT, use this template to report on GitHub:

```markdown
## UAT Issue Report

**Issue Type:** [Bug | Documentation | Enhancement]
**Severity:** [Low | Medium | High | Critical]
**Phase:** [Installation | I2S Output | UART Control | Web UI | Integration]

### Environment
- **BBGW Hardware:** [revision]
- **Debian Version:** [version]
- **Kernel Version:** [version]
- **Project Version:** v1.0.0-bbgw

### Description
[Clear description of the issue]

### Steps to Reproduce
1. [Step 1]
2. [Step 2]
3. [Step 3]

### Expected Behavior
[What should happen]

### Actual Behavior
[What actually happened]

### Logs/Screenshots
```
[Paste relevant logs or attach screenshots]
```

### Workaround
[If known workaround exists]

### Suggested Fix
[If you have ideas for fixing]
```

---

## Acceptance Sign-Off

### Phase 6.3 Completion Criteria

**User Acceptance Testing is complete when:**

- [ ] **At least 3 independent testers** have completed Scenario 1 (Fresh Installation)
- [ ] **At least 2 testers** have completed Scenario 2 (I2S Audio Output)
- [ ] **At least 2 testers** have completed Scenario 3 (UART Control)
- [ ] **At least 2 testers** have completed Scenario 4 (Web UI)
- [ ] **At least 1 tester** has completed Scenario 6 (End-to-End Integration)
- [ ] **All critical/high severity issues** have been fixed
- [ ] **Documentation has been updated** based on feedback
- [ ] **Feedback summary** has been documented

### Sign-Off Checklist

**Project Lead Sign-Off:**
- [ ] All UAT scenarios executed
- [ ] Critical issues resolved
- [ ] Documentation updated per feedback
- [ ] Release notes updated with UAT results

**Date:** __________  
**Signed:** __________

---

## Post-UAT Actions

After UAT completion:

1. **Update RELEASE_NOTES.md:**
   - Add "User Acceptance Testing" section
   - List number of testers
   - Summarize key findings
   - Note any limitations discovered

2. **Update TROUBLESHOOTING_BBGW.md:**
   - Add new issues discovered during UAT
   - Update existing issues with better solutions

3. **Update Documentation:**
   - Fix unclear sections
   - Add missing information
   - Improve examples based on feedback

4. **Create GitHub Milestone:**
   - Title: "Post-v1.0.0-bbgw UAT Fixes"
   - Link all UAT-discovered issues
   - Set priority and timeline

5. **Consider Patch Release:**
   - If critical issues found: v1.0.1-bbgw
   - If documentation only: Update docs without new version
   - If enhancements requested: Plan for v1.1.0-bbgw

---

## Summary

This User Acceptance Testing Guide ensures the BBGW I2S Source project meets real-world user needs through:

- **Comprehensive test scenarios** covering installation, functionality, integration
- **Clear acceptance criteria** for each scenario
- **Feedback collection templates** for systematic issue reporting
- **Documentation quality checks** ensuring clarity and completeness
- **Sign-off process** validating project readiness

**Next Steps:**
1. Share this guide with community testers
2. Set up GitHub Issues template for UAT feedback
3. Monitor feedback and respond promptly
4. Update documentation based on findings
5. Fix critical issues before final release

**Testing Coordinator:** [Assign person]  
**Target Completion:** [Set date]  
**Status:** Ready for community testing

---

**Version:** 1.0.0-bbgw  
**Last Updated:** 2026-02-07  
**Status:** Active
