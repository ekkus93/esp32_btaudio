# ESP32 Bluetooth Audio Source TODO List

This document tracks the implementation tasks for the ESP32 Bluetooth Audio Source project.

## Core Functionality

### Bluetooth A2DP Implementation
- [x] Initial A2DP source profile implementation
  - (6) "Bluetooth stack initializes successfully" [bluetooth]
  - (3) "A2DP starts and stops streaming" [bluetooth][a2dp]

- [x] Implement device scanning functionality
  - (1) "Bluetooth scan starts successfully" [bluetooth][a2dp]
  - (9) "Bluetooth scan reports discovered devices" [bluetooth][a2dp][scan]
  - (10) "Bluetooth scan filters by device type" [bluetooth][a2dp][scan]
  - (11) "Bluetooth scanning basic functionality" [bluetooth][a2dp][scan]
  - (12) "Bluetooth scan filters devices by type" [bluetooth][a2dp][scan]
  - (13) "Bluetooth scan returns device details" [bluetooth][a2dp][scan]
  - (14) "Bluetooth scan times out properly" [bluetooth][a2dp][scan]
  - (15) "Bluetooth scan can be stopped early" [bluetooth][a2dp][scan]

- [x] Implement connection management
  - (2) "Bluetooth connects to A2DP sink" [bluetooth][a2dp]
  - (4) "Bluetooth disconnects properly" [bluetooth][a2dp]
  - (16) "Connect to a device by address" [bluetooth][a2dp][connection]
  - (17) "Connect to a device by name" [bluetooth][a2dp][connection]
  - (18) "Handle connection failure gracefully" [bluetooth][a2dp][connection]
  - (19) "Handle connection timeout" [bluetooth][a2dp][connection]
  - (20) "Get connection status information" [bluetooth][a2dp][connection]

- [x] Add reconnection logic for previously paired devices
  - (5) "A2DP remembers paired devices" [bluetooth][a2dp]
  - (21) "Auto-reconnect when connection drops" [bluetooth][a2dp][connection]
- [x] Handle connection events and state transitions
- [x] Implement audio streaming control (start/stop)
  - (25) "Audio streaming starts successfully" [bluetooth][a2dp][audio]
  - (26) "Audio streaming stops successfully" [bluetooth][a2dp][audio]
  - (27) "Audio streaming cannot start when disconnected" [bluetooth][a2dp][audio]
  - (28) "Audio streaming can be paused and resumed" [bluetooth][a2dp][audio]
  - (29) "Audio streaming state is reported correctly" [bluetooth][a2dp][audio]

### Audio Processing
- [x] Configure I2S driver for receiving audio
- [x] Set up audio data buffers and processing pipeline
- [x] Implement volume control functionality
- [x] Add mute/unmute capability
- [ ] Support for different sample rates
- [ ] Handle audio format conversion if needed

### Pairing and Security
- [ ] Implement "Just Works" pairing method
- [ ] Add PIN-based pairing support
- [ ] Implement Secure Simple Pairing (SSP) with confirmation
- [ ] Add pairing management functionality
- [ ] Add unpair functionality for specific devices
- [ ] Add unpair all devices functionality

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
- [ ] Web-based configuration interface
