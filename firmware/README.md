# Firmware

SDCC 4.6, model `--model-large`, STC8A4K32S2A12 at 18.432 MHz (internal clock).
The clock's user interface (menu, texts, protocol replies) is German.

## The clock

| File | Contents |
|---|---|
| `uhr2.c` | main program: display interrupt, listening pauses and live protocol, DS3231, touch, ADC, alarm and puzzle, melodies, menu, scrolling text, night mode, light organ, settings in EEPROM |
| `anzeige.c` | display sequence (via `#include` in `uhr2.c`): info rotation, transition animations, digit and colon effects, theme 3, weekday, countdown |
| `kalender.c` | calendar in EEPROM (via `#include`), commands `Y…` |
| `schrift.h` | 5×7 text font, **generated** by `host/schrift_erzeugen.py` from `host/schrift_art.py` – do not edit by hand |
| `stc8.h`, `stc8.c` | special function registers, UART (with `-DUART_RX_IRQ` via the clock's ring buffer) |

## Building and flashing

```bash
./build.sh uhr2.c        # -> build/uhr2.hex; memory usage is shown
./flash.sh               # without unplugging: stcgal + isp_reset.sh (command R via the live protocol)
```

- `build.sh` compiles any source (`./build.sh matrix.c`); only `uhr2.c` gets
  `-DUART_RX_IRQ -DBAUD=921600UL`, the diagnostic firmwares run at 57 600 baud.
- `flash.sh` aborts an attempt only while stcgal is still waiting for the
  bootloader, never in the middle of writing, and leaves the EEPROM untouched.
  Environment: `UHR_PORT` (default `/dev/ttyUSB0`), `PYTHON` (interpreter with
  stcgal, default `python3`; with the repo's venv active nothing is needed).
- stcgal runs via `stcgal_pause.py`: it waits 0.3 s (`STC_PAUSE`) before the frequency
  trimming. Without that pause the bootloader (BSL 7.3.11U) discards the
  request and stcgal fails with "Trimming frequency: Protocol error: incorrect
  frame start" (found 2026-11-13).
- **No series resistor in the RX line when flashing.** With the 1 kΩ of the
  ESP wiring between adapter TX and P3.0 (pin 18), stcgal fails with
  "incorrect frame start" during trimming or "bootloader does not answer";
  once it broke off in the middle of writing. With the clamp directly on the
  pin: flashed in attempt 1 and 2 (2026-11-14, found by the user; with the
  resistor back in: 5 of 5 failed, without it again attempt 2). Live
  commands work through the resistor; only the bootloader does not.
- Without running clock firmware: `python3 stcgal_pause.py -P stc8 -t 18432
  -p /dev/ttyUSB0 -o eeprom_erase_enabled=false build/uhr2.hex` and briefly
  disconnect the clock from power. The USB serial adapter must not hang on the
  same hub port as the clock's supply, or it resets too. Without a backup
  battery in the DS3231 the time is lost – set it again with `host/stellen.py`.
- Memory: the program flash (32 KB) is just over 95 % full.

## Diagnostic firmwares (reverse engineering)

These programs were used to decode the board; background in
[docs/PINOUT.md](../docs/PINOUT.md). They talk at 57 600 baud and are partly
controlled by the measurement tools in `host/`.

| File | Purpose |
|---|---|
| `hello.c` | minimal test: compiler, flashing, clock, UART |
| `baudprobe.c` | baud rate probe |
| `census.c` | pin census, two-stage |
| `i2cprobe.c` | searches the I²C bus to the DS3231 |
| `periph.c` | find microphone, light sensor, touch, buzzer, rear LEDs |
| `matrix.c` | matrix scanner: drive pin pairs (with `host/matrix_scan.py` and camera) |
| `muster.c` | test patterns on the matrix |
| `piep.c`, `sqw.c` | search for the buzzer; prove the buzzer on the DS3231's SQW output |
| `touch.c`, `fein.c` | find the touch pads via the discharge time, fine measurement |
| `adc.c` | output all analog inputs continuously |
| `uhr.c` | first clock firmware (predecessor of `uhr2.c`) |
