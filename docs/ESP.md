# Connecting an ESP (ESPHome, Home Assistant)

The clock can be controlled by an ESP8285/ESP8266 running
[ESPHome](https://esphome.io/) and integrated into Home Assistant. The
configuration is in [`esp/matrixuhr.yaml`](../esp/matrixuhr.yaml); the protocol
it speaks is described in [PROTOCOL.md](PROTOCOL.md).

Requires clock firmware from 2026-11-13 or later: older firmware only took
commands up to about 61 bytes reliably.

## Hardware

Tested with an ESP8285 (1 MB flash, embedded) in ESP-12S format on a white
adapter board with pull-ups for EN/RST and a pull-down for GPIO15. An ESP8266
with 4 MB works the same way (`board: esp12e`).

### Voltages

- The **STC8 runs at 5 V** (measured at chip pin 14 against pin 16); its TxD
  (P3.1) drives 5 V levels. The ESP is not 5 V tolerant → voltage divider.
- The clock's **AMS1117-3.3 (U2)** supplies the DS3231. Its 3.3 V output can
  also supply the ESP; put an electrolytic capacitor (220–470 µF) and 100 nF
  close to the ESP for the Wi-Fi current peaks.

### Wiring

The UART pins are on the right side of the chip; see the photo and pin
numbering in [PROTOCOL.md](PROTOCOL.md#pins-on-the-chip).

| Clock | In between | ESP |
|---|---|---|
| pin 18, P3.0 RxD (also matrix column 1) | 1 kΩ in series | GPIO1 (TX) |
| pin 19, P3.1 TxD (also matrix column 2) | 1 kΩ in series, then 2 kΩ to GND (5 V → 3.3 V) | GPIO3 (RX) |
| pin 16, GND | – | GND |
| AMS1117 output, 3.3 V | 220–470 µF + 100 nF | VCC |

- **1 kΩ towards RxD:** outside the listening pauses the clock drives P3.0 hard
  low as matrix column 1; the resistor limits the current against the ESP's
  output. Larger values raise the low level at the pin (P3.0 has a weak
  internal pull-up during reception); 2 kΩ is not recommended.
- Measured on 2026-11-13 through exactly this network (with a FT232R in place
  of the ESP): 50/50 lines at 4, 20, 64, 100, 200 and 254 bytes, also with all
  LEDs on, no check error in 650 lines.

## ESPHome

1. Copy [`esp/matrixuhr.yaml`](../esp/matrixuhr.yaml) into your ESPHome config
   folder and create `secrets.yaml` from
   [`esp/secrets.yaml.example`](../esp/secrets.yaml.example).
2. Flash the ESP the first time over its own USB serial adapter (GPIO0 low at
   reset). With the ESP8285, esptool reports "Flash ID: 0 / Failed to
   communicate with the flash chip" – a false alarm, flashing in DOUT mode
   works. After that, updates go over the air.
3. UART0 (GPIO1/GPIO3) belongs to the clock: `logger: baud_rate: 0`, logs only
   via Wi-Fi.

Checked with ESPHome 2026.10.2: `esphome config` is valid, `esphome compile`
builds (about 470 KB flash of 1 MB, enough room for OTA).

### How the script `uhr_cmd` talks to the clock

- Sync in chunks of 8 × `0xFF` until the clock answers `!`, then command, check
  character and `\n` in one go (no pause per byte; the entity "Bytepause (µs)"
  is only kept for testing and defaults to 0).
- Reply: the first printable line ending in `\r\n` (bytes in between are display
  garbage on TxD). `pruef` → repeat, `zu lang` → give up, no reply → repeat;
  at most 3 attempts.
- Commands with `warten: false` (melody `M`, test alarm `U9`) only wait for the
  `!`, because their reply comes seconds later.
- Calls are queued (`mode: queued`): the next command only starts when the
  previous one has its reply. A line that arrives while a command is still
  running is acknowledged with `!` but dropped by the clock.
- A repeat after a lost reply can execute a command twice (two waves for `O2`,
  scrolling text starts again). Since the 2026-11-13 firmware replies are
  rarely lost; a sequence number in the protocol would avoid it completely
  (not implemented).

### Entities

Names are German, like the clock's user interface.

| Entity | Command |
|---|---|
| Lauftext (scrolling text) | `l…`, at most 253 bytes |
| Helligkeit (0 = Auto) | `H0`…`H8` |
| Countdown starten (Minuten) / Countdown stoppen | `CMM00` / `C0000` |
| Alarm, Alarm-Zeit, Alarm-Melodie, Alarm-Rätsel | `aHHMMx`, `N1`…`N5`, `U0`…`U2` |
| Nachtmodus, Lichtorgel | `Q0`/`Q1`, `O0`/`O1` |
| Animation, Ziffern-Effekt, Doppelpunkt-Effekt, Info-Rotation, Theme, Rück-LEDs, Schrift | `Vw`, `Vz`, `Vd`, `Vi`, `Vt`, `Vl`, `Vs` |
| Buttons: Zeit stellen, Testalarm, Display aus, Melodie probehören, Lichtorgel-Welle | `s…` + `D…`, `U9`, `Q2`, `M…`, `O2` |
| Temperatur, Status roh (v) | `T`, `v` every 5 min (format of `v` in [PROTOCOL.md](PROTOCOL.md#reply-to-v)) |
| Letzte Antwort, Befehl (roh) | diagnostics: last reply, send any command |

The time is set from Home Assistant every day at 3:00. The DS3231 on the
tested board has no working backup battery, so after a power failure the
clock shows 01.01.00 until then – press "Zeit stellen" after a power cut.

### Actions for Home Assistant (calendar)

| Action | Parameters | Command |
|---|---|---|
| `termin_setzen` | monat, tag, jahr (0 = every month/year), stunde (−1 = all day), minute, ton, termin_text | `Y+MMTTJJhhmmfText` |
| `termine_alle_loeschen` | – | `Y-` |
| `termin_loeschen` | platz | `Ydnnn` |
| `termin_anzeigen` | platz | `Ylnnn` |
| `termine_zaehlen` | – | `Y?` |

The calendar text is stored with at most 55 bytes per entry.

## Not included on purpose

`R` and `u` (bootloader/update), `h`/`w` (halt with a different framing) and
the diagnostic commands. Raw commands starting with `a` set the alarm – set it
again in Home Assistant if that happens while testing.

## Flashing the clock through the ESP (idea, not built)

Possible in principle: a "flash mode" in the ESP that sends `R`, switches its
UART to 115 200 baud 8E1 and exposes it as a TCP bridge (`stream_server`); on
the PC, socat creates a virtual serial port for stcgal (handshake and transfer
both at 115 200, via `firmware/stcgal_pause.py`). Open question: whether
ESPHome can change the parity of UART0 at run time on the ESP8266. Until then,
flash the clock with a USB serial adapter (see [firmware/README.md](../firmware/README.md)).
