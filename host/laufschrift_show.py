#!/usr/bin/env python3
"""Runs several texts as scrolling text across the display.

For watching: the clock halts for it, each text runs through once. Afterwards
the clock runs again. Usage:  ../.venv/bin/python laufschrift_show.py [text ...]
"""
import os
import re
import sys
import time

import serial

from anhalten import anhalten

import schrift_art as S

REGISTER = re.compile(rb"^[0-9A-F]{2}( [0-9A-F]{2}){6} *$")
STANDARD = [
    "The quick brown fox jumps over the lazy dog",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz",
    "0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~",
    "Zwölf Boxkämpfer jagen Viktor quer über den großen Sylter Deich - 20 °C, 5 € und § 1",
]

s = serial.Serial(os.environ.get("UHR_PORT", "/dev/ttyUSB0"), 921600, timeout=0.1)


def angehalten():
    t0 = time.time()
    while time.time() - t0 < 25:
        s.reset_input_buffer()
        s.write(b"d")
        s.flush()
        time.sleep(0.6)
        for z in s.read(300).split(b"\n"):
            if REGISTER.match(z.strip()):
                return True
        s.write(b".")
        s.flush()
        time.sleep(0.05)
    return False


if not anhalten(s):
    sys.exit("clock cannot be halted reliably")

for text in (sys.argv[1:] or STANDARD):
    schritte = len(S.strom(text)) + 25
    print(f"{schritte:4d} steps  {text}", flush=True)
    s.reset_input_buffer()
    t0 = time.time()
    s.write(b"l" + text.encode("utf-8") + b"\n")
    s.flush()
    a = b""
    while time.time() - t0 < 120 and b"fertig" not in a:
        a += s.read(200)
    dt = time.time() - t0
    print(f"      {dt:5.1f} s = {schritte / dt:.1f} columns/s\n", flush=True)
    time.sleep(0.5)

s.write(b"w")
s.flush()
time.sleep(0.3)
s.close()
