#!/usr/bin/env python3
"""Pulls pin after pin to 0 V - you set the pace with the Enter key.

Hold a continuity tester between a solder pad of R1 (or R2) and GND. If it
beeps, the pin currently shown is the one being looked for.

A timed sweep was too hasty for that: with the probe in hand one does not
manage a second per pin.

Usage:  ../.venv/bin/python pin_schritt.py
"""
import os
import serial
import time

s = serial.Serial(os.environ.get("UHR_PORT", "/dev/ttyUSB0"), 57600, timeout=0.4)
time.sleep(0.6)
s.reset_input_buffer()


def senden(b, versuche=3):
    """Sends a command and waits for a non-empty reply.

    With a fixed 0.3 s an empty line occasionally came back - then one does
    not know which pin is being driven, and the measurement is worthless.
    """
    for _ in range(versuche):
        s.reset_input_buffer()
        s.write(b)
        s.flush()
        t0 = time.time()
        antwort = b""
        while time.time() - t0 < 1.5:
            antwort += s.read(200)
            if b"\n" in antwort:
                break
        text = antwort.decode("ascii", "replace").strip()
        if text:
            return text
    return "(no reply - is the touch firmware running?)"


print(senden(b"m"))
print("\nEnter = next pin, 'q' = quit\n")
while True:
    if input().strip().lower() == "q":
        break
    antwort = senden(b"n")
    print(antwort)
    if "durch" in antwort:
        break
s.write(b"n")          # release the last pin
s.close()
