#!/usr/bin/env python3
"""stcgal with a short pause before the frequency trimming. Same arguments as stcgal.

Found on 2026-11-13: stcgal sends the trimming request right after the status
packet. The bootloader (BSL 7.3.11U) then often discards it, starts the old
firmware, and stcgal reads its display garbage as a reply ("Trimming
frequency: Protocol error: incorrect frame start"). Recorded by hand, the
bootloader answers the same request correctly when it comes 0.3 s later.
"""
import os
import sys
import time

from stcgal import frontend, protocols

_calibrate = protocols.Stc8Protocol.calibrate
PAUSE = float(os.environ.get("STC_PAUSE", "0.3"))   # seconds


def calibrate(self):
    time.sleep(PAUSE)
    return _calibrate(self)


protocols.Stc8Protocol.calibrate = calibrate
sys.exit(frontend.cli())
