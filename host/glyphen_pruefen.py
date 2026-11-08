"""Compares the firmware's theme 2 glyphs cell by cell with the photos.

On command 'zHHMM' the firmware shows arbitrary digits without changing the
time. Here "16:35" and "23:56" are shown - exactly what the two product
photos show -, the frame buffer is read via 'b' and held against the
patterns read off the photos. The expected values are kept here
independently of the table in the firmware, otherwise it would confirm
itself.

Usage:  ../.venv/bin/python glyphen_pruefen.py
"""
import os
import re
import sys
import time

import numpy as np
import serial

from anhalten import anhalten

# Read off the photos (photo 1: 1,6,3,5,colon; photo 2: 2,3,5,6).
ART = {
    "1": ["..##.", ".###.", "..##.", "..##.", "..##.", "..##.", ".####"],
    "2": ["####.", "...##", "...##", "#####", "##...", "##...", ".####"],
    "3": ["####.", "...##", "...##", "####.", "...##", "...##", "####."],
    "5": ["#####", "##...", "##...", "####.", "...##", "...##", "####."],
    "6": [".###.", "##..#", "##...", "####.", "##.##", "##.##", ".###."],
}
DOPPELPUNKT = 0x36            # rows 2-3 and 5-6
ORTE = (0, 6, 14, 20)         # start column of the four digits


def soll(zeit):
    """Expected frame buffer for four digits from the patterns read off."""
    sp = [0] * 25
    for ziffer, ab in zip(zeit, ORTE):
        for c in range(5):
            sp[ab + c] = sum((1 << r) for r in range(7) if ART[ziffer][r][c] == "#")
    sp[12] = DOPPELPUNKT
    return sp


REGISTER = re.compile(rb"^[0-9A-F]{2}( [0-9A-F]{2}){6} *$")

s = serial.Serial(os.environ.get("UHR_PORT", "/dev/ttyUSB0"), 921600, timeout=0.1)


def angehalten():
    """Only a reply in the exact register format counts.

    While the display runs, the firmware drives P3.1 as a matrix column and
    produces garbage - long enough to fool a merely length-based check. I
    fell for exactly that once.
    """
    t0 = time.time()
    while time.time() - t0 < 25:
        s.reset_input_buffer()
        s.write(b"d")
        s.flush()
        time.sleep(0.6)
        for zeile in s.read(300).split(b"\n"):
            if REGISTER.match(zeile.strip()):
                return True
        s.write(b".")
        s.flush()
        time.sleep(0.05)
    return False


def dump():
    for _ in range(3):
        s.reset_input_buffer()
        s.write(b"b")
        s.flush()
        time.sleep(0.8)
        for z in s.read(300).decode("ascii", "replace").split("\n"):
            z = z.strip()
            if len(z) == 50 and re.fullmatch(r"[0-9A-F]{50}", z):
                return [int(z[i:i + 2], 16) for i in range(0, 50, 2)]
    return None


def male(sp):
    print("    " + "".join(str((i + 1) % 10) for i in range(25)))
    for r in range(7):
        print(f"  {r + 1} " + "".join("#" if (c >> r) & 1 else "." for c in sp))


if not anhalten(s):
    sys.exit("clock cannot be halted reliably")

fehler = 0
for zeit in ("1635", "2356"):
    s.reset_input_buffer()
    s.write(f"z{zeit}".encode())
    s.flush()
    time.sleep(0.8)
    if b"ok" not in s.read(50):
        sys.exit("command z was not acknowledged")
    firm = dump()
    if firm is None:
        sys.exit("no frame buffer dump")
    ref = soll(zeit)
    print(f'Firmware shows "{zeit[:2]}:{zeit[2:]}":')
    male(firm)
    diff = [c for c in range(25) if firm[c] != ref[c]]
    print(f"  -> columns differing from the photos: {len(diff)} of 25")
    for c in diff:
        print(f"     column {c + 1}: firmware 0x{firm[c]:02X}, photo 0x{ref[c]:02X}")
    fehler += len(diff)
    print()

s.write(b"w")
s.flush()
time.sleep(0.3)
s.close()
sys.exit(1 if fehler else 0)
