#!/usr/bin/env python3
"""Sets time and date of the DS3231 to the PC time.

Runs via the protocol during operation (host/live.py), the display keeps
running. The time is set on a second change, so that the offset stays below
the transfer time and does not amount to up to a second.

Usage:  python3 stellen.py      (port: environment variable UHR_PORT, default /dev/ttyUSB0)
"""
import os
import sys
import time

import serial

from live import PORT, senden


def main():
    s = serial.Serial(PORT, 921600, timeout=0.05)

    jetzt = time.localtime()
    datum = f"D{jetzt.tm_mday:02d}{jetzt.tm_mon:02d}{jetzt.tm_year % 100:02d}{jetzt.tm_wday + 1}"
    print("date:", senden(s, datum.encode(), erwartet=rb"datum"))

    # Send shortly before the second change; the line arrives in about 2 ms.
    while time.time() % 1.0 < 0.97:
        time.sleep(0.002)
    t = time.localtime(time.time() + 1)
    zeit = f"s{t.tm_hour:02d}{t.tm_min:02d}{t.tm_sec:02d}"
    antwort = senden(s, zeit.encode(), warte=0.1, erwartet=rb"gestellt")
    print(f"time:  {t.tm_hour:02d}:{t.tm_min:02d}:{t.tm_sec:02d} ->", antwort)
    if antwort is None:
        sys.exit("clock does not answer")

    print("check:", senden(s, b"d"), "PC", time.strftime("%H:%M:%S"))
    s.close()


if __name__ == "__main__":
    main()
