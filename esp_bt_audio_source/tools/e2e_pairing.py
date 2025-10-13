#!/usr/bin/env python3
import serial
import time

PORT = '/dev/ttyUSB0'
BAUD = 115200


def send(s, cmd):
    """Helper to send a command over an open serial object."""
    print('> ' + cmd)
    s.write((cmd + '\n').encode('utf-8'))


def main():
    # Device to pair (from mock devices list used in tests, choose an address present in mocks)
    TARGET = 'AA:BB:CC:11:22:33'

    s = serial.Serial(PORT, BAUD, timeout=1)
    print('Opened', PORT)
    # drain
    time.sleep(0.1)
    s.reset_input_buffer()

    # Send pair command
    # Enable mock mode and add the mock device for reproducible pairing on-device
    send(s, 'CMD|DEBUG MOCK_ON')
    # payload: MAC,NAME
    send(s, 'CMD|DEBUG MOCK_ADD ' + TARGET + ',Test_Device')
    time.sleep(0.5)
    send(s, 'CMD|DEBUG MOCK_PAIR ' + TARGET)

    start = time.time()
    paired = False
    while time.time() - start < 10:
        line = s.readline().decode('utf-8', errors='replace').strip()
        if not line:
            continue
        print('< ' + line)
        parts = line.split('|')
        # expecting EVENT|PAIR|CONFIRM|<addr>,<passkey>
        if len(parts) >= 4 and parts[0] == 'EVENT' and parts[1] == 'PAIR':
            subtype = parts[2]
            if subtype == 'CONFIRM':
                # send confirm
                print('Responding with CONFIRM_PIN 1')
                send(s, 'CMD|CONFIRM_PIN|1')
            elif subtype == 'PIN_REQUEST':
                print('Responding with ENTER_PIN 1234')
                send(s, 'CMD|ENTER_PIN|1234')
            elif subtype == 'SUCCESS':
                print('Pair succeeded')
                paired = True
                break
            elif subtype == 'FAILED':
                print('Pair failed')
                break

    if not paired:
        print('Pairing did not complete within timeout')

    s.close()


if __name__ == '__main__':
    main()
