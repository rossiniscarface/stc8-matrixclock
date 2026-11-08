#!/usr/bin/env python3
"""Redraws the clock's frame buffer as text - without a camera.

Halts the firmware, sets a theme, reads the frame buffer (25 columns,
bit 0 = top row) and paints it as 7 rows.

Usage:  ../.venv/bin/python theme_pruefen.py [1|2]
"""
import os
import sys
import time

import serial

from anhalten import anhalten

s = serial.Serial(os.environ.get("UHR_PORT", "/dev/ttyUSB0"), 921600, timeout=0.1)
def angehalten():
    """Reliably determine that the firmware is in the halted state.

    The message "angehalten" only comes at the transition, once. If the clock
    is already halted - e.g. by a stray character during boot -, one waits
    for it in vain. So first ask with 'd', which is only answered in the
    halted state.
    """
    t0 = time.time()
    while time.time() - t0 < 20:
        s.reset_input_buffer()
        s.write(b"d")
        s.flush()
        time.sleep(0.4)
        antwort = s.read(100)
        if b"\r\n" in antwort and antwort.strip()[:2].isalnum() and len(antwort.strip()) > 10:
            return True
        s.write(b".")
        s.flush()
        time.sleep(0.05)
    return False


if not anhalten(s):
    sys.exit("clock does not halt")

for theme in (sys.argv[1:] or ["1", "2"]):
    s.reset_input_buffer()
    s.write(f"t{theme}".encode())
    s.flush()
    time.sleep(0.5)
    s.reset_input_buffer()
    s.write(b"b")
    s.flush()
    time.sleep(0.6)
    hexa = s.read(200).decode("ascii", "replace").strip()
    if len(hexa) != 50:
        print(f"Theme{theme}: unexpected reply {hexa!r}")
        continue
    spalten = [int(hexa[i:i + 2], 16) for i in range(0, 50, 2)]
    print(f"\nTheme{theme}")
    print("    " + "".join(str((i + 1) % 10) for i in range(25)))
    for zeile in range(7):
        print(f"  {zeile + 1} " + "".join("#" if (c >> zeile) & 1 else "." for c in spalten))

s.write(b"w")
s.flush()
time.sleep(0.3)
s.close()
