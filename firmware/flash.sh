#!/usr/bin/env bash
# Flashes without unplugging: stcgal calls isp_reset.sh, the running clock
# jumps into the bootloader.
#
# An attempt is only aborted while stcgal is still waiting for the
# bootloader. As soon as it has answered, stcgal runs to completion
# undisturbed - a fixed time limit once aborted in the middle of writing and
# left half a program behind.
#
# The EEPROM (settings, calendar) is left untouched when flashing
# (eeprom_erase_enabled=false).
#
# Usage: ./flash.sh [hex] (default build/uhr2.hex)
# Environment: UHR_PORT (default /dev/ttyUSB0), RESET=... other reset script,
#              STC_BAUD for isp_reset.sh (default 921600)
#              PYTHON: interpreter with stcgal installed (default python3)
#
# stcgal runs via stcgal_pause.py, which waits 0.3 s before the frequency
# trimming - without it the bootloader discards the request (see there).
cd "$(dirname "$0")"
HEX="${1:-build/uhr2.hex}"
PORT="${UHR_PORT:-/dev/ttyUSB0}"
RESET="${RESET:-./isp_reset.sh}"
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

for versuch in 1 2 3 4 5; do
    "${PYTHON:-python3}" -u ./stcgal_pause.py -P stc8 -t 18432 -p "$PORT" -o eeprom_erase_enabled=false -a -r "$RESET" "$HEX" > "$LOG" 2>&1 &
    pid=$!
    verbunden=0
    for _ in $(seq 1 60); do                    # wait 6 s for the bootloader
        sleep 0.1
        if grep -q "Target model" "$LOG"; then verbunden=1; break; fi
        kill -0 "$pid" 2>/dev/null || break
    done
    if [ "$verbunden" = 0 ] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid"; wait "$pid" 2>/dev/null
        echo "attempt $versuch: bootloader does not answer"
        continue
    fi
    wait "$pid"; rc=$?
    if [ "$rc" = 0 ]; then
        echo "flashed in attempt $versuch"
        exit 0
    fi
    echo "attempt $versuch: stcgal error $rc"; tr '\r' '\n' < "$LOG" | tail -3
done
exit 1
