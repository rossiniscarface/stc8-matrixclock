"""Halts the running clock firmware - shared by all test scripts.

The firmware listens in short pauses before each frame (see uhr2.c). The
command 'h' goes via the protocol: 0xFF sequence, command, check character,
end of line. Afterwards the display is off and the firmware reads the UART
directly.

Whether it really is halted is only shown by a reply in the exact register
format to 'd'. A merely length-based check is fooled by garbage bytes.
"""
import re
import time

REGISTER = re.compile(rb"^[0-9A-F]{2}( [0-9A-F]{2}){6} *$")


def live_zeile(befehl):
    """Clock protocol: 0xFF sequence (longer than the pause interval; 150 bytes
    at 921600 baud), command, check character, end of line."""
    return b"\xff" * 150 + befehl + bytes([0x40 + (sum(befehl) & 0x3F)]) + b"\n"


def ist_angehalten(s):
    s.reset_input_buffer()
    s.write(b"d")
    s.flush()
    time.sleep(0.5)
    return any(REGISTER.match(z.strip()) for z in s.read(400).split(b"\n"))


def anhalten(s, maxsek=30):
    """True as soon as the firmware is reliably halted."""
    if ist_angehalten(s):
        return True
    t0 = time.time()
    while time.time() - t0 < maxsek:
        # Live protocol (docs/PROTOCOL.md): 0xFF sequence, then 'h', check
        # character and end of line. Send several times in case an attempt is lost.
        for _ in range(10):
            s.write(live_zeile(b"h"))
            s.flush()
            time.sleep(0.02)
        time.sleep(0.05)
        if ist_angehalten(s):
            return True
    return False
