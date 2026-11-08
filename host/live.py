"""Commands to the running clock (live protocol, see docs/PROTOCOL.md).

Line = 0xFF without gaps (longer than the pause interval of 1.2 ms; at 921600
baud 150 bytes), command,
check character 0x40 + (sum & 0x3F), end of line. The clock acknowledges the
sync with '!' and answers the command. Repeats only if no reply comes.
"""
import os
import re
import sys
import time

import serial

PORT = os.environ.get("UHR_PORT", os.environ.get("UHR_PORT", "/dev/ttyUSB0"))


def zeile(befehl):
    return b"\xff" * 150 + befehl + bytes([0x40 + (sum(befehl) & 0x3F)]) + b"\n"


def senden(s, befehl, warte=0.25, versuche=40, erwartet=rb"\r\n"):
    for i in range(versuche):
        s.reset_input_buffer()
        s.write(zeile(befehl))
        s.flush()
        time.sleep(warte)
        r = s.read(max(400, s.in_waiting))     # everything, even after long commands
        # acknowledgement '!', then the reply line. In between, garbage bytes from the
        # display come in (TxD is column 2); they are never printable.
        m = re.search(rb"!.*?([ -~]+\r\n)", r, re.S)
        if m and re.search(erwartet, m.group(1)):
            return m.group(1).strip()
    return None


if __name__ == "__main__":
    s = serial.Serial(PORT, 921600, timeout=0.05)
    print(senden(s, " ".join(sys.argv[1:]).encode()))
