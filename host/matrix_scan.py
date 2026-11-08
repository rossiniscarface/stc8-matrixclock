#!/usr/bin/env python3
"""Runs through all pin pairs and notes which LED lights up.

An LED needs a source and a sink, hence pairs instead of single pins. For
each pair, what changes in the camera image while the pair is driven is
measured - and the brightest spot in it is located.

Four lessons from aborted runs are built in here:

  * The dark reference is taken anew BEFORE EACH PAIR. A reference measured
    once at the beginning was the design flaw of the first run: the camera
    image drifts at one spot from 30 to over 200, and calculated against the
    old reference this grew into a permanent hit that reported every pair as
    lit from measurement 61 on. 297 hits, almost all wrong.

  * Reference and measurement are each averaged over three images. Single
    images are noisy with peaks up to 57 - too close to the threshold.
    Averaged, 26 remain, with a signal of 243: factor 9 instead of factor 1.

  * The measurement image must come from the window in which the LED was
    really lit. A late image shows darkness - a false negative that looks
    like a real result.

  * A known LED is measured regularly as well. In one go it checks whether
    the controller still answers, whether the wiring holds and whether the
    camera has not moved.

Every attempt is logged, not just the hits; a restart skips what has already
been measured.

Usage:  ../.venv/bin/python matrix_scan.py
"""
import json
import math
import os
import sys
import time

import numpy as np
import serial

from kamera import Kamera

PORT = os.environ.get("UHR_PORT", os.environ.get("UHR_PORT", "/dev/ttyUSB0"))
BAUD = 57600
STREAM = os.environ.get("KAMERA_URL", "http://kamera.local:8080/")  # MJPEG stream of the ESP32-Cam
ZUSTAND = "scan.json"

PINS = (
    [(0, b) for b in range(8)]
    + [(1, b) for b in range(6)]
    + [(2, b) for b in range(7)]
    + [(3, b) for b in range(2, 8)]
    + [(4, 0), (4, 1)]
)

MITTEL = 3             # images per reference and per measurement
FENSTER = 25           # search window around the peak, in pixels
SCHWELLE = 90          # measured: background up to 26, signal 243
MIN_PIXEL = 8

# The firmware drives the pair for about 1 s. An image taken after that
# shows an LED that has already gone out.
FENSTER_ENDE = 0.95

# Control LED: the strongest in the pre-test with brightness 243, reproducible
# to within +-2 over five repetitions.
REF_QUELLE = (2, 0)
REF_SENKE = (0, 0)
REF_ALLE = 20          # re-check every this many pairs
REF_HELL_MIN = 0.5     # fraction of the start brightness that must at least remain
REF_VERSATZ_MAX = 15.0  # allowed; the centroid itself already scatters by about 6 px


def schwerpunkt(d):
    y, x = np.unravel_index(int(np.argmax(d)), d.shape)
    spitze = float(d[y, x])
    y0, y1 = max(0, y - FENSTER), min(d.shape[0], y + FENSTER + 1)
    x0, x1 = max(0, x - FENSTER), min(d.shape[1], x + FENSTER + 1)
    aus = d[y0:y1, x0:x1].copy()
    aus[aus < spitze * 0.4] = 0.0
    flaeche = int((aus > 0).sum())
    s = aus.sum()
    if s <= 0:
        return spitze, float(x), float(y), flaeche
    yy, xx = np.mgrid[y0:y1, x0:x1]
    return spitze, float((xx * aus).sum() / s), float((yy * aus).sum() / s), flaeche


def bilder(cam, n, bis=None):
    """Averages n images. With 'bis' only those taken before it."""
    g = []
    while len(g) < n:
        f, t = cam.frame_nach_mit_zeit(time.time())
        if bis is not None and t > bis:
            break
        g.append(f)
    return (np.mean(g, axis=0) if g else None), len(g)


def messen(ser, cam, qp, qb, sp, sb, versuche=2):
    """Drives a pair and evaluates. None if the controller is silent."""
    knapp = 0
    for _ in range(versuche):
        # Reference first - here everything is guaranteed high-impedance.
        ref, _ = bilder(cam, MITTEL)

        ser.reset_input_buffer()
        t_befehl = time.time()
        ser.write(f"x{qp}{qb}{sp}{sb}".encode())
        ser.flush()
        cam.frame_nach(t_befehl + 0.10)
        f, n = bilder(cam, MITTEL, bis=t_befehl + FENSTER_ENDE)

        t0 = time.time()
        quittung = b""
        while time.time() - t0 < 3.0:
            quittung += ser.read(32)
            if b"ok" in quittung or b"gesperrt" in quittung:
                break
        if b"ok" not in quittung and b"gesperrt" not in quittung:
            return None

        if n >= 2:
            return schwerpunkt(f - ref)
        knapp = n

    raise TimeoutError(
        f"only {knapp} image(s) in the lit window instead of {MITTEL}. The stream "
        f"has become too slow; every result would be a false negative.")


def laden():
    if not os.path.exists(ZUSTAND):
        return {}
    try:
        with open(ZUSTAND) as fh:
            return {f"{e['quelle']}>{e['senke']}": e for e in json.load(fh)}
    except Exception:
        return {}


def sichern(daten):
    with open(ZUSTAND, "w") as fh:
        json.dump(list(daten.values()), fh, indent=1)


def main():
    daten = laden()
    if daten:
        print(f"{len(daten)} measurements from an earlier run, will be skipped.\n")

    try:
        ser = serial.Serial(PORT, BAUD, timeout=1.0)
    except Exception as e:
        sys.exit(f"cannot open port: {e}")
    time.sleep(0.4)
    ser.reset_input_buffer()
    ser.write(b"o")
    ser.flush()
    time.sleep(0.6)
    ser.reset_input_buffer()

    cam = Kamera(STREAM)
    print("waiting for the stream ...", flush=True)
    cam.frame_nach(time.time())
    print(f"Stream running. Reference is taken fresh per pair from {MITTEL} images, "
          f"threshold {SCHWELLE}.")

    k = messen(ser, cam, *REF_QUELLE, *REF_SENKE)
    if k is None or k[0] < SCHWELLE:
        sys.exit("The control LED does not light. Check the wiring "
                 "before the run starts.")
    soll_hell, soll_x, soll_y, _ = k
    print(f"control LED P{REF_QUELLE[0]}.{REF_QUELLE[1]} -> "
          f"P{REF_SENKE[0]}.{REF_SENKE[1]}: "
          f"brightness {soll_hell:.0f} at ({soll_x:.1f},{soll_y:.1f})\n")

    paare = [(q, s) for q in PINS for s in PINS if q != s]
    offen = [p for p in paare
             if f"P{p[0][0]}.{p[0][1]}>P{p[1][0]}.{p[1][1]}" not in daten]
    print(f"{len(paare)} pairs in total, {len(offen)} open\n")

    t_start = time.time()
    abbruch = None
    for i, ((qp, qb), (sp, sb)) in enumerate(offen, 1):
        try:
            e = messen(ser, cam, qp, qb, sp, sb)
        except TimeoutError as ex:
            abbruch = f"camera: {ex}"
            break
        if e is None:
            abbruch = "The controller no longer acknowledges."
            break

        spitze, cx, cy, flaeche = e
        leuchtet = spitze >= SCHWELLE and flaeche >= MIN_PIXEL
        daten[f"P{qp}.{qb}>P{sp}.{sb}"] = {
            "quelle": f"P{qp}.{qb}", "senke": f"P{sp}.{sb}",
            "leuchtet": leuchtet, "spitze": round(spitze, 1),
            "flaeche": flaeche, "x": round(cx, 1), "y": round(cy, 1),
        }
        if leuchtet:
            print(f"  [{i:>3}/{len(offen)}] P{qp}.{qb} -> P{sp}.{sb}   "
                  f"brightness {spitze:5.0f}  area {flaeche:4}  "
                  f"({cx:6.1f},{cy:6.1f})")

        # --- regular check on the known LED ---
        if i % REF_ALLE == 0:
            try:
                k = messen(ser, cam, *REF_QUELLE, *REF_SENKE)
            except TimeoutError as ex:
                abbruch = f"camera: {ex}"
                break
            n = sum(1 for x in daten.values() if x["leuchtet"])
            rest = (time.time() - t_start) / i * (len(offen) - i)
            if k is None:
                abbruch = "Check: no acknowledgement from the controller."
                break
            hell, kx, ky, _ = k
            versatz = math.hypot(kx - soll_x, ky - soll_y)
            ok_hell = hell >= soll_hell * REF_HELL_MIN
            ok_pos = versatz <= REF_VERSATZ_MAX
            zeichen = "ok " if (ok_hell and ok_pos) else "!! "
            print(f"  [{i:>3}/{len(offen)}] CONNECTION {zeichen}"
                  f"control LED brightness {hell:3.0f}/{soll_hell:.0f}  "
                  f"offset {versatz:4.1f}px  |  {n} hits, "
                  f"about {rest / 60:.0f} min left", flush=True)
            if not ok_hell:
                abbruch = (f"control LED too dark ({hell:.0f} instead of "
                           f"{soll_hell:.0f}). Clamp loose?")
                break
            if not ok_pos:
                abbruch = (f"control LED shifted by {versatz:.1f}px. "
                           f"Camera or board moved - all coordinates "
                           f"would be wrong from here on.")
                break
            sichern(daten)

    ser.write(b"o")
    ser.flush()
    ser.close()
    cam.stop()
    sichern(daten)

    n = sum(1 for e in daten.values() if e["leuchtet"])
    print()
    if abbruch:
        print(f"  ABORTED: {abbruch}")
        print(f"  {len(daten)} measurements saved. A restart continues "
              f"from there.")
    print(f"{len(daten)} of {len(paare)} pairs measured, {n} light up.")


if __name__ == "__main__":
    main()
