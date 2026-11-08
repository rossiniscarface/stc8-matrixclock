#!/usr/bin/env python3
"""Checks hands-free what can be checked: alarm enable across a restart and
the storage of the theme.

Alarm: first the alarm is switched on via 'a', then the clock restarts via
the bootloader, afterwards the enable (bit 0 of register 0E) is there again -
or not. That was exactly the bug: buzzer(0) at start-up overwrote it.

Usage:  ../.venv/bin/python bedienung_pruefen.py
"""
import os
import re
import sys
import time

import serial

from anhalten import anhalten

REGISTER = re.compile(rb"^[0-9A-F]{2}( [0-9A-F]{2}){6} *$")
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


def frage(befehl, warten=0.8):
    s.reset_input_buffer()
    s.write(befehl)
    s.flush()
    time.sleep(warten)
    return s.read(300).decode("ascii", "replace").strip()


def register():
    m = re.search(r"0B=([0-9A-F]{2}) 0E=([0-9A-F]{2}) 0F=([0-9A-F]{2})", frage(b"r"))
    return tuple(int(x, 16) for x in m.groups()) if m else None


def pruefe(name, ok, detail=""):
    print(f"{'ok  ' if ok else 'FAIL'} {name}{('  ' + detail) if detail else ''}")
    return 0 if ok else 1


if not anhalten(s):
    sys.exit("clock cannot be halted reliably")
fehler = 0

print("-- alarm enable across restart --")
frage(b"a07301")                      # 07:30, on
r = register()
fehler += pruefe("after setting: enable on", r is not None and r[1] & 1, f"0E={r[1]:02X}" if r else "")
s.write(b"R"); s.flush(); time.sleep(3.0)          # restart via the bootloader
if not anhalten(s):
    sys.exit("cannot halt after restart")
r = register()
fehler += pruefe("after restart: enable still on", r is not None and r[1] & 1, f"0E={r[1]:02X}" if r else "")
frage(b"a07300")                      # off again
r = register()
fehler += pruefe("switched off: enable off", r is not None and not (r[1] & 1), f"0E={r[1]:02X}" if r else "")
s.write(b"R"); s.flush(); time.sleep(3.0)
if not anhalten(s):
    sys.exit("cannot halt after second restart")
r = register()
fehler += pruefe("after restart still off", r is not None and not (r[1] & 1), f"0E={r[1]:02X}" if r else "")

print("\n-- theme stored --")
for t in (b"t2", b"t1", b"t2"):
    frage(t)
    r = register()
    want = int(t[1:])
    fehler += pruefe(f"{t.decode()}: Register 0B", r is not None and r[0] == want, f"0B={r[0]:02X}" if r else "")

s.write(b"w"); s.flush(); time.sleep(0.3); s.close()
print(f"\n{'all fine' if not fehler else str(fehler) + ' check(s) failed'}")
sys.exit(1 if fehler else 0)
