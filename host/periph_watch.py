#!/usr/bin/env python3
"""Finds the microphone and light sensor by waiting for stimuli.

First it measures for a few seconds at rest, then the script asks for
clapping and a hand over the sensor. Whichever ADC channel then clearly
increases its range is the component being looked for.

Usage:  ../.venv/bin/python periph_watch.py
"""
import os
import re
import statistics as st
import sys
import time

import serial

PORT = os.environ.get("UHR_PORT", os.environ.get("UHR_PORT", "/dev/ttyUSB0"))
BAUD = 57600
PIN = {k: (f"P1.{k}" if k < 8 else f"P0.{k - 8}") for k in range(15)}


def sammeln(ser, sekunden):
    """Continuously requests ADC sweeps and returns the raw lines."""
    reihen = []
    ende = time.time() + sekunden
    while time.time() < ende:
        ser.write(b"a")
        ser.flush()
        zeile = b""
        while b"\n" not in zeile:
            ch = ser.read(1)
            if not ch:
                break
            zeile += ch
        m = re.match(rb"ADC((?:\s+\d+){15})", zeile.strip())
        if m:
            reihen.append([int(x) for x in m.group(1).split()])
    return reihen


def spannen(reihen):
    return [max(c) - min(c) for c in zip(*reihen)] if reihen else [0] * 15


def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.6)
    except Exception as e:
        sys.exit(f"cannot open port: {e}")
    time.sleep(0.3)
    ser.reset_input_buffer()

    print("measuring at rest, please do nothing ...", flush=True)
    ruhe = sammeln(ser, 5.0)

    print()
    print("=" * 60)
    print("  NOW:  play a loud tone on the phone, put the phone on the board")
    print("          - and then TAKE YOUR HANDS AWAY.")
    print("          No clapping: moving hands cast shadows on")
    print("          the LDR and that falsifies the measurement.")
    print("          -- the next 15 seconds --")
    print("=" * 60, flush=True)
    aktiv = sammeln(ser, 15.0)
    ser.close()

    sr, sa = spannen(ruhe), spannen(aktiv)
    print(f"\n{len(ruhe)} rest / {len(aktiv)} active sweeps\n")
    print("  chan.  pin     range_rest   range_active   factor")
    treffer = []
    for k in range(15):
        f = (sa[k] / sr[k]) if sr[k] else float("inf") if sa[k] else 1.0
        # Only evaluate channels that are quiet at rest. The threshold is
        # deliberately split in two: P1.7 sits almost at ground, there even a
        # few counts are a clear signal.
        marke = ""
        ruhig = sr[k] < 60
        if ruhig and (sa[k] > 150 or (sa[k] >= 3 * max(sr[k], 1) and sa[k] > 15)):
            marke = "   <== REACTS"
            treffer.append((k, sr[k], sa[k]))
        print(f"   {k:>2}    {PIN[k]:<5} {sr[k]:>10}  {sa[k]:>12}   {f:>6.1f}{marke}")

    print()
    if treffer:
        for k, r, a in treffer:
            print(f"  channel {k} ({PIN[k]}) reacts to the stimulus: "
                  f"range {r} -> {a}")
    else:
        print("  No channel with a quiet base state reacted clearly.")
        print("  The floating matrix channels are very noisy anyway and")
        print("  are therefore not counted as hits.")


if __name__ == "__main__":
    main()
