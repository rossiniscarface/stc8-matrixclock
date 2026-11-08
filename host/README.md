# PC tools

Python 3 with `pyserial` (measurement and camera tools additionally `numpy`,
`pillow`). Port via the environment variable `UHR_PORT` (default
`/dev/ttyUSB0`).

## Everyday use (running clock firmware, 921 600 baud)

| Script | Purpose |
|---|---|
| `live.py` | send a command via the live protocol, e.g. `python3 live.py d` or `python3 live.py "lHallo"`; also serves as a library (`zeile()`, `senden()`) |
| `stellen.py` | take time and date from the PC |
| `anhalten.py` | library: halts the clock via `h` (for test scripts), then `w` continue |
| `laufschrift_show.py` | show several texts as scrolling text |

## Test scripts (halt the clock, resume it afterwards)

| Script | Checks | Note |
|---|---|---|
| `laufschrift_pruefen.py` | font and scrolling text byte by byte against `schrift_art.py` | set font 2 (thin): `live.py Vs2` |
| `glyphen_pruefen.py` | 5×7 digits against the product photos | set font 1; needs `numpy` |
| `theme_pruefen.py` | frame buffer of the themes as text | |
| `bedienung_pruefen.py` | alarm enable and theme across a restart | |

## Font

| Script | Purpose |
|---|---|
| `schrift_art.py` | **source** of the 5×7 font as ASCII patterns, with reference scrolling text |
| `schrift_erzeugen.py` | generates `firmware/schrift.h` from it |

## Measurement tools from the reverse engineering (57 600 baud, diagnostic firmwares)

| Script | Purpose |
|---|---|
| `matrix_scan.py` | run through all pin pairs, find the lit LED via camera (`KAMERA_URL`, MJPEG of the ESP32-Cam); result `scan.json` |
| `kamera.py` | read the MJPEG stream (library for `matrix_scan.py`) |
| `led_suche.py`, `aktor_test.py`, `buzzer_suche.py`, `pin_schritt.py`, `uart_pins.py` | clock pins one after another: find rear LEDs, buzzer, remaining pins |
| `periph_watch.py`, `touch_test.py` | find microphone, light sensor and touch via stimuli or discharge time |
