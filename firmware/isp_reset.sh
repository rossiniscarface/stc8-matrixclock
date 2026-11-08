#!/usr/bin/env bash
# Called by stcgal via --resetcmd and replaces the power cycle.
#
# Sends 'R' to the running firmware. It then sets IAP_CONTR to 0x60 and
# restarts into the bootloader. Only works while a firmware that knows this
# command is running — otherwise only manual unplugging remains.
#
# The crux on which the first version failed: at this point stcgal already
# has the port open and set to 2400 baud with EVEN PARITY for the
# handshake. A tty shares these settings process-wide.
# Simply setting 'stty raw 57600' here erases stcgal's parity, and
# its sync pulses then go nowhere — the loop waits forever.
# Therefore: save the state with 'stty -g' and restore it exactly afterwards.
set -u

PORT="${UHR_PORT:-/dev/ttyUSB0}"
BAUD="${STC_BAUD:-921600}"

if [ ! -e "$PORT" ]; then
    echo "isp_reset: $PORT does not exist" >&2
    exit 1
fi

gesichert=$(stty -F "$PORT" -g 2>/dev/null) || {
    echo "isp_reset: cannot read port settings" >&2
    exit 1
}
zurueck() { stty -F "$PORT" "$gesichert" 2>/dev/null; }
trap zurueck EXIT

stty -F "$PORT" "$BAUD" cs8 -parenb -cstopb -echo raw 2>/dev/null || {
    echo "isp_reset: stty fehlgeschlagen" >&2
    exit 1
}

# Clock protocol (see uhr2.c, "Listening while displaying"): 0xFF without
# gaps, longer than one frame (5.3 ms = 600 bytes at 921600), then 'R'
# with check character ('R' happens to have 'R') and end of line. A single
# write, so that no gap appears in the stream. In the UPDATE menu the 'R'
# works the same way.
ff=""
for _ in $(seq 1 600); do ff="$ff\377"; done
printf "${ff}RR\n" > "$PORT" || { echo "isp_reset: Schreiben fehlgeschlagen" >&2; exit 1; }

# Wait until the 603 bytes are out (6.5 ms) before the port setting is
# restored - otherwise the baud rate changes in the middle of the stream. It
# must not be longer: after the reset the bootloader is only ready for about
# 0.2 s, and while this script sleeps, stcgal is not pulsing yet.
sleep 0.03
