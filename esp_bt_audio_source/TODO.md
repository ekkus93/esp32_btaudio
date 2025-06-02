# ESP32 Bluetooth Audio Source TODO List

This document tracks the implementation tasks for the ESP32 Bluetooth Audio Source project.

## Core Functionality

### Bluetooth A2DP Implementation
- [x] Initial A2DP source profile implementation
  - (1) "Bluetooth stack initializes successfully" [bluetooth]
  - (16) "A2DP starts and stops streaming" [bluetooth][a2dp]

- [x] Implement device scanning functionality
  - (2) "Bluetooth scan starts successfully" [bluetooth][a2dp]
  - (3) "Bluetooth scan reports discovered devices" [bluetooth][a2dp][scan]
  - (4) "Bluetooth scan filters by device type" [bluetooth][a2dp][scan]
  - (5) "Bluetooth scanning basic functionality" [bluetooth][a2dp][scan]
  - (6) "Bluetooth scan returns device details" [bluetooth][a2dp][scan]
  - (7) "Bluetooth scan times out properly" [bluetooth][a2dp][scan]
  - (8) "Bluetooth scan can be stopped early" [bluetooth][a2dp][scan]

- [x] Implement connection management
  - (15) "Bluetooth connects to A2DP sink" [bluetooth][a2dp]
  - (9) "Bluetooth disconnects properly" [bluetooth][a2dp]
  - (9) "Connect to a device by address" [bluetooth][a2dp][connection]
  - (10) "Connect to a device by name" [bluetooth][a2dp][connection]
  - (11) "Handle connection failure gracefully" [bluetooth][a2dp][connection]
  - (12) "Handle connection timeout" [bluetooth][a2dp][connection]
  - (13) "Get connection status information" [bluetooth][a2dp][connection]

- [x] Add reconnection logic for previously paired devices
  - (17) "A2DP remembers paired devices" [bluetooth][a2dp]
  - (14) "Auto-reconnect when connection drops" [bluetooth][a2dp][connection]
- [x] Handle connection events and state transitions
- [x] Implement audio streaming control (start/stop)
  - (18) "Audio streaming starts successfully" [bluetooth][a2dp][audio]
  - (19) "Audio streaming stops successfully" [bluetooth][a2dp][audio]
  - (20) "Audio streaming cannot start when disconnected" [bluetooth][a2dp][audio]
  - (21) "Audio streaming can be paused and resumed" [bluetooth][a2dp][audio]
  - (22) "Audio streaming state is reported correctly" [bluetooth][a2dp][audio]

### Audio Processing
- [x] Configure I2S driver for receiving audio
  - (23) "I2S driver initializes successfully" [i2s][audio]
  - (24) "I2S standard mode configuration works" [i2s][audio]
- [x] Set up audio data buffers and processing pipeline
  - (25) "Audio buffer pool initialization" [audio][pipeline]
  - (26) "Audio buffer allocation and release" [audio][pipeline]
  - (27) "Single-stage audio processing" [audio][pipeline]
  - (28) "Multi-stage audio processing pipeline" [audio][pipeline]
  - (29) "Basic audio buffer operations" [audio][pipeline]
- [x] Implement volume control functionality
  - (30) "Volume level setting and retrieval" [audio][volume]
  - (31) "Volume application to audio samples" [audio][volume]
- [x] Add mute/unmute capability
  - (32) "Audio mute/unmute operations" [audio][volume]
- [x] Support for different sample rates
  - (33) "Sample rate configuration" [audio][sampling]
  - (34) "Buffer size calculations for different rates" [audio][sampling]
  - (35) "Sample rate conversion" [audio][sampling]
- [x] PCM format validation tests (bit depth, endianness)
  - (36) "16-bit PCM format handling" [audio][pcm]
  - (37) "24-bit PCM format handling" [audio][pcm]
  - (38) "PCM endianness conversion" [audio][pcm]
  - (39) "Bit depth conversion (16/24/32-bit)" [audio][pcm]
- [x] I2S channel configuration tests (mono/stereo handling)
  - (40) "Mono channel configuration" [i2s][channels]
  - (41) "Stereo channel configuration" [i2s][channels]
  - (42) "Stereo to mono conversion" [i2s][channels]
  - (43) "Mono to stereo conversion" [i2s][channels]
  - (44) "Channel independence" [i2s][channels]

### Pairing and Security
- [x] Implement "Just Works" pairing method
  - (45) "Just Works pairing initiation" [bluetooth][pairing]
  - (46) "Just Works pairing completion" [bluetooth][pairing]
  - (47) "Just Works pairing failure handling" [bluetooth][pairing]
- [x] Add PIN-based pairing support
  - (45) "PIN-based pairing initiation" [bluetooth][pairing]
  - (46) "PIN-based pairing successful completion" [bluetooth][pairing]
  - (47) "PIN-based pairing failure handling" [bluetooth][pairing]
  - (48) "PIN-based pairing timeout handling" [bluetooth][pairing]
  - (49) "Setting and retrieving default PIN" [bluetooth][pairing]
- [x] Implement Secure Simple Pairing (SSP) with confirmation
  - (50) "SSP confirmation request" [bluetooth][pairing][ssp]
  - (51) "SSP confirmation accepted" [bluetooth][pairing][ssp]
  - (52) "SSP confirmation rejected" [bluetooth][pairing][ssp]
  - (53) "SSP fallback to PIN" [bluetooth][pairing][ssp]
- [x] Add pairing management functionality
  - (54) "Unpairing a specific device" [bluetooth][pairing][management]
  - (55) "Unpairing all devices" [bluetooth][pairing][management]
  - (56) "Paired devices persistence" [bluetooth][pairing][management]
  - (57) "Paired devices retrieval" [bluetooth][pairing][management]
  - (58) "Paired device connection info" [bluetooth][pairing][management]
- [x] Add unpair functionality for specific devices
  - (59) "Unpairing a nonexistent device" [bluetooth][pairing][management]
  - (60) "Unpairing a connected device" [bluetooth][pairing][management]
  - (61) "Unpairing with invalid address" [bluetooth][pairing][management]
  - (62) "Unpair persistence" [bluetooth][pairing][management]
- [x] Add unpair all devices functionality
  - (63) "Unpair all when no devices are paired" [bluetooth][pairing][management]
  - (64) "Unpair all with connected devices" [bluetooth][pairing][management]
  - (65) "Unpair all persistence" [bluetooth][pairing][management]
  - (66) "Unpair all with multiple devices" [bluetooth][pairing][management]

## Command Interface

### Command Protocol Implementation
- [ ] Set up UART communication for commands
- [ ] Implement command parser
- [ ] Add command executor
- [ ] Implement response formatter
- [ ] Add event notification system

### Command Handlers
- [ ] SCAN command handler
- [ ] CONNECT/CONNECT_NAME command handlers
- [ ] DISCONNECT command handler
- [ ] PAIRED devices listing handler
- [ ] SET_NAME command handler
- [ ] START/STOP audio streaming handlers
- [ ] VOLUME/MUTE/UNMUTE command handlers
- [ ] PAIR/UNPAIR command handlers
- [ ] PIN confirmation and entry handlers
- [ ] STATUS/VERSION/RESET/DEBUG command handlers
- [ ] SAMPLE_RATE/I2S_CONFIG handlers

## Persistent Storage

- [ ] Set up NVS (Non-Volatile Storage)
- [ ] Store paired devices information
- [ ] Save configuration settings
- [ ] Store preferred volume levels
- [ ] Save custom device name

## Testing Framework

### Host-Based Testing
- [ ] Set up CMake build system for host tests
- [ ] Create mock implementations of ESP-IDF components
- [ ] Implement unit tests for command parser
- [ ] Implement unit tests for Bluetooth functionality
- [ ] Create tests for audio processing pipeline

### On-Device Testing
- [x] Set up basic Unity test framework
- [x] Implement initial test cases
- [ ] Add comprehensive test suite for all components
- [ ] Create integration tests for full system

## Documentation

- [ ] Document API for each component
- [ ] Create detailed connection diagrams
- [ ] Add troubleshooting guide
- [ ] Create user manual for common operations
- [ ] Document test procedures

## Optimization and Robustness

- [ ] Add error handling for all operations
- [ ] Implement watchdog timers
- [ ] Optimize memory usage
- [ ] Reduce power consumption
- [ ] Handle edge cases (disconnections, timeouts, etc.)
- [ ] Stress testing under various conditions

## Future Enhancements

- [ ] Support for AVRCP profile for remote control
- [ ] Audio quality improvements (SBC vs AAC vs aptX)
- [ ] Multiple simultaneous connections (if hardware supports)
- [ ] OTA firmware updates
