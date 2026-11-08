#!/usr/bin/env python3
"""Checks the firmware's font and scrolling text against the template.

For several texts, the column stream that the firmware builds from UTF-8
('L' loads, 'S' reads it as hex) is compared byte by byte with what
schrift_art.strom() computes from the ASCII patterns. Plus individual scroll
windows ('W', then 'b'). That checks the table, edge trimming, letter
spacing, space width, UTF-8 conversion and the window logic.

Usage:  ../.venv/bin/python laufschrift_pruefen.py
"""
import os
import re
import sys
import time

import serial

from anhalten import anhalten

import schrift_art as S

REGISTER = re.compile(rb"^[0-9A-F]{2}( [0-9A-F]{2}){6} *$")
s = serial.Serial(os.environ.get("UHR_PORT", "/dev/ttyUSB0"), 921600, timeout=0.1)


def angehalten():
    """Only a reply in the exact register format counts (see glyphen_pruefen)."""
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


def zeile_lesen(maxsek=3.0):
    t0, a = time.time(), b""
    while time.time() - t0 < maxsek:
        a += s.read(2000)
        if a.endswith(b"\n"):
            break
    return a.decode("ascii", "replace").strip()


def laden(text):
    s.reset_input_buffer()
    s.write(b"L" + text.encode("utf-8") + b"\n")
    s.flush()
    r = zeile_lesen()
    m = re.fullmatch(r"ok (\d+)", r)
    return int(m.group(1)) if m else None


def strom_lesen():
    s.reset_input_buffer()
    s.write(b"S")
    s.flush()
    z = zeile_lesen(5.0)
    if not re.fullmatch(r"[0-9A-F]*", z) or len(z) % 2:
        return None
    return [int(z[i:i + 2], 16) for i in range(0, len(z), 2)]


def fenster_lesen(off):
    s.reset_input_buffer()
    s.write(f"W{off:04d}".encode())
    s.flush()
    if "ok" not in zeile_lesen():
        return None
    s.reset_input_buffer()
    s.write(b"b")
    s.flush()
    z = zeile_lesen()
    if len(z) != 50 or not re.fullmatch(r"[0-9A-F]{50}", z):
        return None
    return [int(z[i:i + 2], 16) for i in range(0, 50, 2)]


def soll_fenster(strom, off):
    out = []
    for c in range(25):
        pos = off + c - 24
        out.append(0 if pos < 0 or pos >= len(strom) else strom[pos])
    return out


def male(sp):
    for r in range(7):
        print("    " + "".join("█" if (c >> r) & 1 else "·" for c in sp))


TEXTE = [
    ("pangram", "The quick brown fox jumps over the lazy dog"),
    ("capitals", "ABCDEFGHIJKLMNOPQRSTUVWXYZ"),
    ("lowercase", "abcdefghijklmnopqrstuvwxyz"),
    ("digits", "0123456789"),
    ("punctuation", "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"),
    ("umlauts", "ÄÖÜ äöü ß ° € §"),
    ("German", "Zwölf Boxkämpfer jagen Viktor quer über den großen Sylter Deich"),
    ("unknown", "ñ und 😀 sind nicht in der Schrift"),
    ("spaces", "a  b   c"),
]

if not anhalten(s):
    sys.exit("clock cannot be halted reliably")

fehler = 0
for name, text in TEXTE:
    soll = S.strom(text)
    n = laden(text)
    ist = strom_lesen()
    ok_len = n == len(soll)
    ok_strom = ist == soll
    fenster_ok = True
    detail = ""
    if ist is not None:
        for off in (0, 7, len(soll) // 2, len(soll) + 24):
            f = fenster_lesen(off)
            if f != soll_fenster(soll, off):
                fenster_ok = False
                detail += f" window@{off}"
    ok = ok_len and ok_strom and fenster_ok
    fehler += 0 if ok else 1
    print(f"{'ok  ' if ok else 'FAIL'} {name:16} {len(soll):4d} columns"
          f"{'' if ok_len else '  length ' + str(n)}"
          f"{'' if ok_strom else '  stream differs'}{detail}")
    if ist is not None and not ok_strom:
        diff = [i for i in range(min(len(ist), len(soll))) if ist[i] != soll[i]]
        if diff:
            i = diff[0]
            print(f"     first difference at column {i}: firmware 0x{ist[i]:02X}, template 0x{soll[i]:02X}")

print()
print(f"{len(TEXTE) - fehler} of {len(TEXTE)} texts without difference")
s.write(b"w")
s.flush()
time.sleep(0.3)
s.close()
sys.exit(1 if fehler else 0)
