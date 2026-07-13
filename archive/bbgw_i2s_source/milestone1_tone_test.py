#!/usr/bin/env python3
"""
Milestone 1: Basic I2S Tone Generation Test (BeagleBone Green Wireless)

This script demonstrates all Milestone 1 deliverables:
1. Python script generates 1 kHz sine tone using NumPy
2. McASP I2S master transmitter outputs tone to P9.31/29/28 (via ALSA)
3. Continuous playback for 5 minutes without dropouts

Usage:
    # On BeagleBone Green Wireless with McASP I2S configured:
    python3 milestone1_tone_test.py
    
    # With custom duration:
    python3 milestone1_tone_test.py --duration 300  # 5 minutes
    
Hardware Setup:
    - BeagleBone Green Wireless with McASP I2S Device Tree overlay enabled
    - Verify overlay: ls /lib/firmware/BB-BBGW-I2S-00A0.dtbo
    - Enable in /boot/uEnv.txt: uboot_overlay_addr4=/lib/firmware/BB-BBGW-I2S-00A0.dtbo
    - Connect ESP32 esp_bt_audio_source I2S input:
        - P9.31 (BCLK/ACLKX) → ESP32 GPIO 26 (I2S BCLK)
        - P9.29 (WS/FSX)     → ESP32 GPIO 25 (I2S WS)
        - P9.28 (DOUT/AXR1)  → ESP32 GPIO 22 (I2S DIN)
        - P9.1 or P9.2 (GND) → ESP32 GND
    - Bluetooth speaker paired with ESP32
    
Expected Results:
    - 1 kHz tone audible on Bluetooth speaker
    - No I2S errors or underruns
    - Continuous playback for specified duration
    - Console shows real-time statistics

Logic Analyzer Verification (if available):
    - BCLK: 1.536 MHz (48000 Hz × 32 bits)
    - WS: 48 kHz (left/right channel clock)
    - DOUT: Valid PCM data, MSB-first

BeagleBone Green Wireless I2S Source Project
Author: BeagleBone Green Wireless I2S Audio Source Project
Date: 2026-02-07
"""

import sys
import time
import signal
import argparse
from pathlib import Path

# Add project root to path
project_root = Path(__file__).parent
sys.path.insert(0, str(project_root))

from config.manager import ConfigManager
from audio.ring_buffer import RingBuffer
from audio.engine import AudioEngine
from audio.i2s_driver import I2SDriverALSA


class Milestone1Test:
    """
    Milestone 1 test orchestrator.
    
    Coordinates AudioEngine (tone generation) and I2SDriver (McASP I2S transmission)
    for continuous 1 kHz tone playback.
    """
    
    def __init__(self, duration: int = 60):
        """
        Initialize test components.
        
        Args:
            duration: Test duration in seconds (default 60, milestone requires 300)
        """
        self.duration = duration
        self.running = False
        
        # Create minimal config for testing
        self.config = self._create_test_config()
        
        # Initialize components
        self.ring_buffer = RingBuffer(capacity=8192)
        self.audio_engine = AudioEngine(self.config, self.ring_buffer)
        self.i2s_driver = I2SDriverALSA(self.config, self.ring_buffer)
        
        # Statistics
        self.start_time = None
        self.last_stats_time = None
        
    def _create_test_config(self):
        """Create minimal configuration for Milestone 1 test."""
        # Use default config if available, otherwise create minimal config
        config_path = project_root / 'config' / 'config.yaml'
        
        if config_path.exists():
            config = ConfigManager(str(config_path))
        else:
            # Create minimal in-memory config for BBGW
            config = ConfigManager()
            # Set defaults for Milestone 1
            config.config = {
                'i2s': {
                    'device': 'hw:CARD=BBGW-I2S,DEV=0',  # BBGW McASP ALSA device
                    'sample_rate': 48000,
                    'channels': 2,
                    'format': 'S16_LE',
                    'period_size': 1024,
                    'buffer_size': 4096
                },
                'audio': {
                    'tone_freq': 1000,  # 1 kHz tone per Milestone 1
                    'tone_amp': 0.5     # 50% amplitude
                }
            }
        
        return config
    
    def start(self):
        """Start tone generation and I2S transmission."""
        print("=" * 70)
        print("Milestone 1: Basic I2S Tone Generation Test (BBGW)")
        print("=" * 70)
        print()
        print("Configuration:")
        print(f"  Sample Rate:    {self.config.get('i2s.sample_rate')} Hz")
        print(f"  Channels:       {self.config.get('i2s.channels')}")
        print(f"  Format:         {self.config.get('i2s.format')}")
        print(f"  ALSA Device:    {self.config.get('i2s.device')}")
        print(f"  Tone Frequency: {self.config.get('audio.tone_freq')} Hz")
        print(f"  Tone Amplitude: {self.config.get('audio.tone_amp')}")
        print(f"  Test Duration:  {self.duration} seconds")
        print()
        print("I2S McASP Pins (BeagleBone P9 Header):")
        print("  BCLK (ACLKX): P9.31 → ESP32 GPIO 26")
        print("  WS (FSX):     P9.29 → ESP32 GPIO 25")
        print("  DOUT (AXR1):  P9.28 → ESP32 GPIO 22")
        print("  GND:          P9.1  → ESP32 GND")
        print()
        print("Expected I2S Signals:")
        print("  BCLK: 1.536 MHz (48 kHz × 32 bits)")
        print("  WS:   48 kHz (left/right channel clock)")
        print("  DOUT: 16-bit PCM sine wave data")
        print()
        print("Device Tree Overlay:")
        print("  Required: BB-BBGW-I2S-00A0.dtbo")
        print("  Check: ls /lib/firmware/BB-BBGW-I2S-00A0.dtbo")
        print("  Verify: aplay -l | grep BBGW-I2S")
        print()
        print("-" * 70)
        print()
        
        try:
            # Start audio engine (generates tone into ring buffer)
            print("[1/3] Starting audio engine...")
            self.audio_engine.set_source('tone')
            self.audio_engine.set_tone_params(
                freq=self.config.get('audio.tone_freq'),
                amp=self.config.get('audio.tone_amp'),
                mode='mono'
            )
            self.audio_engine.start()
            print("      ✓ Audio engine running (1 kHz tone generation)")
            
            # Brief delay to let buffer fill
            time.sleep(0.5)
            
            # Start I2S driver (transmits samples via ALSA/McASP)
            print("[2/3] Starting I2S driver (McASP)...")
            self.i2s_driver.start()
            print("      ✓ I2S driver running (ALSA/McASP transmission)")
            
            # Monitor playback
            print(f"[3/3] Monitoring playback for {self.duration} seconds...")
            print()
            print("Real-time Statistics:")
            print("-" * 70)
            
            self.running = True
            self.start_time = time.time()
            self.last_stats_time = self.start_time
            
            # Run for specified duration
            while self.running and (time.time() - self.start_time) < self.duration:
                self._print_statistics()
                time.sleep(1.0)
            
            print()
            print("-" * 70)
            print("✓ Test completed successfully!")
            print()
            self._print_final_statistics()
            
        except KeyboardInterrupt:
            print()
            print()
            print("Test interrupted by user")
            self._print_final_statistics()
        
        except Exception as e:
            print()
            print(f"✗ Test failed: {e}")
            import traceback
            traceback.print_exc()
        
        finally:
            self.stop()
    
    def stop(self):
        """Stop audio engine and I2S driver."""
        self.running = False
        print()
        print("Stopping components...")
        
        if hasattr(self, 'i2s_driver'):
            self.i2s_driver.stop()
            print("  ✓ I2S driver stopped")
        
        if hasattr(self, 'audio_engine'):
            self.audio_engine.stop()
            print("  ✓ Audio engine stopped")
    
    def _print_statistics(self):
        """Print real-time statistics (called every second)."""
        now = time.time()
        elapsed = now - self.start_time
        
        # Get statistics from components
        engine_state = self.audio_engine.get_state()
        i2s_stats = {
            'frames_sent': self.i2s_driver.frames_sent,
            'underruns': self.i2s_driver.underruns,
            'buffer_fill': self.ring_buffer.fill_percentage()
        }
        
        # Calculate frame rate
        if hasattr(self, '_last_frames'):
            frames_delta = i2s_stats['frames_sent'] - self._last_frames
            frame_rate = frames_delta / (now - self.last_stats_time)
        else:
            frame_rate = 0
        
        self._last_frames = i2s_stats['frames_sent']
        self.last_stats_time = now
        
        # Print status line (overwrite previous)
        status = (
            f"Time: {elapsed:6.1f}s | "
            f"Frames: {i2s_stats['frames_sent']:10d} | "
            f"Rate: {frame_rate:7.0f} fps | "
            f"Buffer: {i2s_stats['buffer_fill']:5.1f}% | "
            f"Underruns: {i2s_stats['underruns']:3d}"
        )
        print(f"\r{status}", end='', flush=True)
    
    def _print_final_statistics(self):
        """Print final test statistics."""
        elapsed = time.time() - self.start_time
        total_frames = self.i2s_driver.frames_sent
        total_underruns = self.i2s_driver.underruns
        avg_frame_rate = total_frames / elapsed if elapsed > 0 else 0
        
        print("Final Statistics:")
        print(f"  Total Duration:  {elapsed:.1f} seconds")
        print(f"  Total Frames:    {total_frames:,}")
        print(f"  Average Rate:    {avg_frame_rate:.1f} frames/sec")
        print(f"  Expected Rate:   48000 frames/sec")
        print(f"  Total Underruns: {total_underruns}")
        print()
        
        # Success criteria check
        print("Milestone 1 Success Criteria:")
        
        # Criterion 1: Tone audible (manual verification required)
        print("  [MANUAL] Tone audible on Bluetooth speaker")
        
        # Criterion 2: Zero I2S protocol errors
        if total_underruns == 0:
            print(f"  [  ✓  ] Zero I2S underruns: {total_underruns}")
        else:
            print(f"  [  ✗  ] I2S underruns detected: {total_underruns}")
        
        # Criterion 3: Continuous playback for required duration
        required_duration = 300  # 5 minutes
        if elapsed >= required_duration:
            print(f"  [  ✓  ] Continuous playback ≥ {required_duration}s: {elapsed:.1f}s")
        else:
            print(f"  [  -  ] Playback duration: {elapsed:.1f}s (milestone requires {required_duration}s)")
        
        print()
        print("Next Steps:")
        print("  1. Verify BCLK/WS/DOUT with logic analyzer (if available)")
        print("  2. Confirm audio output on Bluetooth speaker (manual)")
        print("  3. Run full 5-minute test: python3 milestone1_tone_test.py --duration 300")
        print("  4. Verify McASP Device Tree overlay: ./overlays/verify_mcasp.sh")
        print()


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="Milestone 1: Basic I2S Tone Generation Test (BeagleBone Green Wireless)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument(
        '--duration',
        type=int,
        default=60,
        help='Test duration in seconds (milestone requires 300)'
    )
    
    args = parser.parse_args()
    
    # Create and run test
    test = Milestone1Test(duration=args.duration)
    
    # Handle Ctrl+C gracefully
    def signal_handler(sig, frame):
        print()
        test.stop()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    
    # Run test
    test.start()


if __name__ == '__main__':
    main()
