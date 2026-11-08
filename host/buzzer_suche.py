#!/usr/bin/env python3
"""Searches for the buzzer: each remaining pin individually, announced.

Candidates are all pins that are not matrix, I2C, light sensor or rear LED.
P4.5-P4.7 are included although they were considered "not bonded out" - the
same unverified assumption was already wrong once for P4.2.

Per pin: 1.3 s constant low, 1.3 s constant high (for an active buzzer with
its own oscillator), then a sweep tone 1.2-6 kHz (for a passive one).

Usage:  ../.venv/bin/python buzzer_suche.py
"""
import os
import time

import serial

PORT = os.environ.get("UHR_PORT", os.environ.get("UHR_PORT", "/dev/ttyUSB0"))
KANDIDATEN = [(1, 7), (4, 3), (4, 4), (4, 5), (4, 6), (4, 7)]

s = serial.Serial(PORT, 57600, timeout=0.3)
time.sleep(0.6)
s.reset_input_buffer()

print("Listen. Each pin gets about 6 seconds.\n")
for i, (p, b) in enumerate(KANDIDATEN, 1):
    print(f"  [{i}/{len(KANDIDATEN)}]  >>> P{p}.{b} <<<   "
          f"constant low, then constant high, then sweep tone", flush=True)
    s.reset_input_buffer()
    s.write(f"a{p}{b}".encode())
    s.flush()
    t0 = time.time()
    antwort = b""
    while time.time() - t0 < 20:
        antwort += s.read(64)
        if b"fertig" in antwort or b"nur Port" in antwort:
            break
    if b"nur Port" in antwort:
        print("        rejected")
    time.sleep(2.0)

s.close()
print("\nDone. At which number did it beep?")
