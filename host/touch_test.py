#!/usr/bin/env python3
"""Finds the touch pads via their discharge time.

The upward measurement was a dead end: no pin has a high-resistance pull-up
to Vcc. In the opposite direction, however, P2.7 stands out - it discharges
in about 194 counts, while all floating pins hold their charge and the
pull-down pins drop immediately. That is the signature of a 1 MOhm path to
ground.

A finger adds capacitance and should lengthen the discharge time.
Measurement takes place in four phases, the second rest phase is the
cross-check.

Usage:  ../.venv/bin/python touch_test.py
"""
import os
import re
import statistics as st
import sys
import time

import serial

PORT = os.environ.get("UHR_PORT", os.environ.get("UHR_PORT", "/dev/ttyUSB0"))
BAUD = 57600
GRENZE = 1500          # counter limit of the firmware


def sammeln(ser, sekunden):
    """Polls 't' and returns {pin: [discharge times]}."""
    werte = {}
    ende = time.time() + sekunden
    while time.time() < ende:
        ser.write(b"t")
        ser.flush()
        zeile = b""
        t0 = time.time()
        while b"\n" not in zeile and time.time() - t0 < 5:
            ch = ser.read(1)
            if not ch:
                break
            zeile += ch
        for pin, _auf, ab in re.findall(rb"(P\d\.\d)=(\d+)/(\d+)", zeile):
            werte.setdefault(pin.decode(), []).append(int(ab))
    return werte


def phase(ser, text, sekunden):
    print()
    print("=" * 56)
    print(f"  {text}")
    print(f"  -- {sekunden:.0f} seconds --")
    print("=" * 56, flush=True)
    return sammeln(ser, sekunden)


def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1.0)
    except Exception as e:
        sys.exit(f"cannot open port: {e}")
    time.sleep(0.3)
    ser.reset_input_buffer()

    print("measuring at rest, please touch nothing ...", flush=True)
    r1 = sammeln(ser, 5.0)
    d = phase(ser, "put a finger on the TRIANGLE and hold", 6.0)
    r2 = phase(ser, "let go, touch nothing", 5.0)
    k = phase(ser, "put a finger on the CIRCLE and hold", 6.0)
    ser.close()

    if not r1:
        sys.exit("No readings - is the periph firmware running?")
    n = len(next(iter(r1.values())))
    print(f"\n\n  {n} sweeps per phase (discharge time, limit {GRENZE})\n")
    print("  pin      rest1  triangle   rest2   circle")
    print("  " + "-" * 46)

    befund = []
    for p in sorted(r1):
        a = st.median(r1.get(p, [0]))
        b = st.median(d.get(p, [0]))
        c = st.median(r2.get(p, [0]))
        e = st.median(k.get(p, [0]))

        # Everything that moves OR lies in the middle range is shown.
        #
        # Two lessons from earlier attempts are built in here: first, it must not
        # only be checked for a rise - a finger drains the charge to ground, the
        # value DROPS. Second, the interesting pins must stay visible even when
        # they do not move, otherwise it is impossible to tell whether anything
        # was measured at all.
        spanne = max(a, b, c, e) - min(a, b, c, e)
        mittelfeld = any(0 < v < GRENZE for v in (a, b, c, e))
        if spanne < 40 and not mittelfeld:
            continue
        print(f"  {p:<6} {a:8.0f} {b:8.0f} {c:8.0f} {e:8.0f}")

        basis = (a + c) / 2
        for wert, welche in ((b, "TRIANGLE"), (e, "CIRCLE")):
            if abs(wert - basis) > max(40.0, basis * 0.25):
                richtung = "falls" if wert < basis else "rises"
                befund.append((p, welche, basis, wert, richtung))

    print()
    if befund:
        for p, welche, basis, wert, richtung in befund:
            print(f"  {p} reacts to {welche}: {basis:.0f} -> {wert:.0f}  ({richtung})")
    else:
        print("  Nothing moved between the phases.")
        print("  Then only continuity-testing R1/R2 against the pads helps.")


if __name__ == "__main__":
    main()
