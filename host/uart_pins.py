#!/usr/bin/env python3
"""Tests P3.0 and P3.1 - the last pins not yet checked.

They carry the UART and therefore cannot be driven in dialogue: whoever pulls
them low cuts their own connection. So the firmware announces first, waits
3 s and then drives them to 3.3 V for 8 s.

Hold a voltmeter between the 2.5 V node of R1 or R2 and GND: if it jumps
towards 3 V during the 8 seconds, the node hangs on this pin.

With the continuity tester this was not visible - nobody beeps through 1 MOhm.

Usage:  ../.venv/bin/python uart_pins.py
"""
import os
import time

import serial

s = serial.Serial(os.environ.get("UHR_PORT", "/dev/ttyUSB0"), 57600, timeout=0.4)
time.sleep(0.6)
s.reset_input_buffer()

for bit in ("0", "1"):
    input(f"\nPress Enter, then P3.{bit} gets 3 seconds of lead time ...")
    s.reset_input_buffer()
    s.write(f"u{bit}".encode())
    s.flush()
    t0 = time.time()
    gesagt = b""
    while time.time() - t0 < 20:
        gesagt += s.read(200)
        if b"vorbei" in gesagt:
            break
    for zeile in gesagt.decode("ascii", "replace").split("\n"):
        if zeile.strip():
            print("   ", zeile.strip())

s.close()
print("\nBoth done.")
