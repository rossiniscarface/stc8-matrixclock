# Matrix clock with custom firmware (STC8A4K32S2A12)

Custom firmware for the LED matrix clock kit "DIY DS3231 Clock"
(Geekcreit/Banggood, 25×7 LEDs, touch keys, DS3231, microphone, light sensor).
The board has been fully mapped and the original firmware replaced.

Besides the firmware, the repo contains the reverse-engineering tools, test
scripts and a **digital twin**: the same firmware runs on the PC and plays
operating sequences on a replica matrix.

> **Language:** the user interface of the clock is implemented in **German** –
> menu items (Zeit, Datum, Alarm, Klang, Quiz, …), option texts, weekday names
> (Mon, Die, Mit, …), the replies of the serial protocol (`gestellt`,
> `alarm gesetzt`, …) and the serial messages of the diagnostic firmwares.
> Source code identifiers (functions, variables, file names) are German as well. The
> documentation and code comments are in English; where a German UI text
> appears, it is quoted as shown on the clock, with a translation alongside.

## What the clock does

- Time in three layouts (H:M:S narrow, HH:MM wide, HH:MM with small seconds),
  three fonts for digits and text
- Info rotation with date, weekday and calendar entries, 8 transition
  animations, 5 digit effects, 5 colon effects
- Alarm with 5 melodies (including a low 520 Hz tone in the smoke-alarm
  pattern) and a switch-off puzzle (symbol sequence or arithmetic problem),
  emergency stop after 10 minutes
- Countdown, calendar (about 500 entries in EEPROM), scrolling text with umlauts
- Brightness automatic from the light sensor or fixed (8 levels, 2–91 %)
- Night mode: display off in the dark, clapping wakes it
- Light organ: noises throw waves across the matrix
- Rear LEDs: off, constant, follow the sound, breathing
- Menu via the two touch keys, texts in 5×7 with plain-text values
- Control during operation via the serial port (for an ESP) without the
  display flickering; firmware update without unplugging

Operation: [docs/USAGE.md](docs/USAGE.md) ·
commands for ESP/PC: [docs/PROTOCOL.md](docs/PROTOCOL.md)

## Directories

| Path | Contents |
|---|---|
| [`firmware/`](firmware/) | Firmware (`uhr2.c`, `anzeige.c`, `kalender.c`), building and flashing, diagnostic firmwares from the reverse engineering |
| [`host/`](host/) | PC tools: live commands, setting the clock, test scripts, font source, measurement tools |
| [`zwilling/`](zwilling/) | Digital twin ("Zwilling"): firmware on the PC, scripts, viewer in the browser |
| [`docs/`](docs/) | Usage, protocol, pinout, development notes, wish list |

## Quick start

Requires: Linux, [SDCC](https://sdcc.sourceforge.net/) (tested with 4.6),
[stcgal](https://github.com/grigorig/stcgal) (1.10), Python 3 with `pyserial`,
a USB serial adapter on RxD (P3.0), TxD (P3.1) and GND – tested with a
3.3 V FT232R, clamped directly onto the chip pins (details in
[docs/PROTOCOL.md](docs/PROTOCOL.md#pins-on-the-chip), with photo).

```bash
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt      # pyserial, stcgal, numpy, pillow

cd firmware
./build.sh uhr2.c          # -> build/uhr2.hex
./flash.sh                 # flashes without unplugging (requires running clock firmware)

cd ../host
python3 stellen.py         # take time and date from the PC
python3 live.py d          # read clock registers
python3 live.py "lHallo Welt"   # scrolling text
```

Port: environment variable `UHR_PORT` (default `/dev/ttyUSB0`).

**First flash** (still the original firmware, or no running clock firmware):
call stcgal without reset and briefly disconnect the clock from power:

```bash
stcgal -P stc8 -t 18432 -p /dev/ttyUSB0 -o eeprom_erase_enabled=false build/uhr2.hex
```

The STC bootloader cannot read back the flash: the original firmware cannot be
backed up and is gone after the first flash.

## Digital twin

```bash
cd zwilling
./bau.sh                   # compile the firmware for the PC with gcc
ZW_SKRIPT=drehbuch/menue.txt ZW_AUSGABE=laeufe/menue.jsonl ./uhr2_pc
python3 betrachter.py      # -> open betrachter.html in the browser
```

Details: [zwilling/README.md](zwilling/README.md).

## Hardware in brief

STC8A4K32S2A12 (LQFP44, 1T 8051, internal clock 18.432 MHz, 32 KB flash,
32 KB EEPROM, 4 KB XRAM), DS3231 on P5.4/P5.5, matrix of 25 columns (sink) ×
7 rows (source P2.0–P2.6) without series resistors, touch P4.3/P4.4, light
sensor P1.6, microphone P1.7, rear LEDs P2.7, buzzer on the DS3231's SQW
output. Complete: [docs/PINOUT.md](docs/PINOUT.md).

## Pitfalls

In detail with measurements in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md); what
is still planned or decided: [docs/WISHLIST.md](docs/WISHLIST.md).

- SDCC's startup routine does not initialise XRAM variables on the STC8
  (`MOVX @R0` with P2) – the firmware copies the initial values itself.
- RxD/TxD are also matrix columns; reception only happens in short listening
  pauses at 921 600 baud.
- stcgal: always pass the port with `-p` (as a positional argument it is read
  as the EEPROM file).

## License

GPL-3.0-or-later, see [LICENSE](LICENSE). Anyone who distributes a modified
firmware – including pre-flashed in a clock – must provide its source code
under the same license.
