#!/usr/bin/env python3
"""Clocks the conspicuous pins one after another - you hear or see which one.

Only pins with known circuitry are tested, not the floating matrix pins: two
with a strong pull-up (candidates for the buzzer via Q1) and two with a
strong pull-down.

Each pin is clocked at about 2 kHz for one second. Three seconds of pause in
between, so that the assignment stays unambiguous.

Usage:  ../.venv/bin/python aktor_test.py
"""
import os
import sys
import time

import serial

PORT = os.environ.get("UHR_PORT", os.environ.get("UHR_PORT", "/dev/ttyUSB0"))
BAUD = 57600

KANDIDATEN = [
    ("4", "3", "strong pull-up  - buzzer via Q1?"),
    ("4", "4", "strong pull-up  - buzzer via Q1?"),
    ("0", "6", "strong pull-down"),
    ("1", "7", "strong pull-down"),
]


def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1.0)
    except Exception as e:
        sys.exit(f"cannot open port: {e}")
    time.sleep(0.3)
    ser.reset_input_buffer()

    print("Watch for a tone AND for the two LEDs on the back.\n")
    for p, b, hinweis in KANDIDATEN:
        print("=" * 56)
        print(f"  NOW:  P{p}.{b}    ({hinweis})")
        print("=" * 56, flush=True)
        ser.reset_input_buffer()
        ser.write(f"d{p}{b}".encode())
        ser.flush()

        # wait for the firmware's reply ("takte ... / fertig")
        t0 = time.time()
        antwort = b""
        while time.time() - t0 < 6:
            antwort += ser.read(64)
            if b"fertig" in antwort or b"gesperrt" in antwort:
                break
        if b"gesperrt" in antwort:
            print("  -> locked by the firmware\n")
        else:
            print("  -> done\n")
        time.sleep(3.0)

    ser.close()
    print("Done. Which pin beeped, which one moved the LEDs?")


if __name__ == "__main__":
    main()
