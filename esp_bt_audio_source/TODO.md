# ESP32 Bluetooth Audio Source TODO List

This document tracks the implementation tasks for the ESP32 Bluetooth Audio Source project.

## Core Functionality

### Bluetooth A2DP Implementation
- [x] Initial A2DP source profile implementation
- [x] Implement device scanning functionality
- [ ] Implement connection management
- [ ] Add reconnection logic for previously paired devices
- [ ] Handle connection events and state transitions
- [ ] Implement audio streaming control (start/stop)

### Audio Processing
- [ ] Configure I2S driver for receiving audio
- [ ] Set up audio data buffers and processing pipeline
- [ ] Implement volume control functionality
- [ ] Add mute/unmute capability
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
