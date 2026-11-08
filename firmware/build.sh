#!/usr/bin/env bash
# Compiles a firmware for the STC8A4K32S2A12 and produces Intel HEX.
# Usage: ./build.sh [source.c]   (default: hello.c)
set -euo pipefail
cd "$(dirname "$0")"

SRC="${1:-hello.c}"
BASE="$(basename "$SRC" .c)"
OUT=build
mkdir -p "$OUT"

# --model-large puts variables into external RAM by default. Needed since
# the firmware has grown: the 8051 has only 128 bytes of directly
# addressable memory, and the temporaries of the menu functions alone
# exceed that. The STC8, on the other hand, has 4 KB of XRAM. Accessing it is
# somewhat slower, which does not matter anywhere here - the display is
# clocked in microseconds anyway, not nanoseconds.
GEMEINSAM="-mmcs51 --std-c99 --model-large --iram-size 256 --xram-size 4096 --code-size 32768"

# The clock firmware receives via interrupt into a ring buffer, at 921600 baud
# (short listening pauses, see uhr2.c).
EXTRA=""
[ "$BASE" = uhr2 ] && EXTRA="-DUART_RX_IRQ -DBAUD=921600UL"

sdcc $GEMEINSAM $EXTRA -c stc8.c -o "$OUT/stc8_$BASE.rel"
sdcc $GEMEINSAM $EXTRA -o "$OUT/$BASE.ihx" "$SRC" "$OUT/stc8_$BASE.rel"

packihx "$OUT/$BASE.ihx" > "$OUT/$BASE.hex"

echo
echo "Done: $OUT/$BASE.hex"
grep -E 'CODE|XRAM|IRAM' "$OUT/$BASE.mem" 2>/dev/null | sed 's/^/  /' || true
