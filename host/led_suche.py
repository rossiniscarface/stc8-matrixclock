#!/usr/bin/env python3
"""Clocks the 30 floating pins one after another - looking for the rear LEDs.

Background: the LEDs light up during flashing. After reset all ports are
quasi-bidirectional with latch 1, i.e. weakly pulled to Vcc - that is already
enough for an LED with its anode at the pin and cathode at ground to glow.
So a pin at high must make them light up.

Each pin is clocked at 2 kHz for one second (half brightness, clearly
visible), then two seconds of pause. Only one pin is ever active, all others
stay high-impedance - so no current flows through the matrix.

Usage:  ../.venv/bin/python led_suche.py
"""
import os
import sys
import time

import serial

PORT = os.environ.get("UHR_PORT", os.environ.get("UHR_PORT", "/dev/ttyUSB0"))
BAUD = 57600

# The 30 floating pins, plus the four with known circuitry.
# P4.3 already ran without a tone, but the other three not yet
# with a correct time base - so they run again here.
PINS = (
    [(0, b) for b in range(8)]
    + [(1, b) for b in range(6)]
    + [(2, b) for b in range(8)]
    + [(3, b) for b in range(2, 8)]
    + [(4, 0), (4, 1)]
    + [(4, 3), (4, 4), (0, 6), (1, 7)]
)
PINS = list(dict.fromkeys(PINS))      # P0.6 would otherwise appear twice


def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1.0)
    except Exception as e:
        sys.exit(f"cannot open port: {e}")
    time.sleep(0.3)
    ser.reset_input_buffer()

    print(f"{len(PINS)} pins, 1 s tone/light and 2 s pause each.")
    print("Watch the two LEDs on the back - and listen too,")
    print("the buzzer might just as well turn up.\n")

    for i, (p, b) in enumerate(PINS, 1):
        print(f"  [{i:>2}/{len(PINS)}]  P{p}.{b}", flush=True)
        ser.reset_input_buffer()
        ser.write(f"d{p}{b}".encode())
        ser.flush()
        t0 = time.time()
        antwort = b""
        while time.time() - t0 < 8:
            antwort += ser.read(64)
            if b"fertig" in antwort or b"gesperrt" in antwort:
                break
        if b"gesperrt" in antwort:
            print("        (locked)")
        time.sleep(2.0)

    ser.close()
    print("\nDone. At which number did the LEDs light up?")
    print("And if something beeped: at which one?")


if __name__ == "__main__":
    main()
