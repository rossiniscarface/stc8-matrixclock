#!/usr/bin/env bash
# Compiles the firmware for the PC (digital twin).
set -e
cd "$(dirname "$0")"
gcc -std=gnu99 -O1 -w -DZWILLING -DUART_RX_IRQ -DBAUD=921600UL -I../firmware \
    -o uhr2_pc ../firmware/uhr2.c zwilling.c
echo "Done: zwilling/uhr2_pc"
