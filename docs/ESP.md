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
- The clock's **AMS1117-3.3 (U2)** supplies the DS3231. **Do not supply the
  ESP from it:** measured on 2026-11-14, the ESP's Wi-Fi current peaks on this
  rail raise the microphone noise above the wake-up threshold (see
  [Known problems](#known-problems-measured-2026-11-14)). Give the ESP its own
  3.3 V regulator (e.g. from the 5 V input), with an electrolytic capacitor
  (220–470 µF) and 100 nF close to the ESP; connect only GND to the clock.

### Wiring

The UART pins are on the right side of the chip; see the photo and pin
numbering in [PROTOCOL.md](PROTOCOL.md#pins-on-the-chip).

| Clock | In between | ESP |
|---|---|---|
| pin 18, P3.0 RxD (also matrix column 1) | 1 kΩ in series | GPIO1 (TX) |
| pin 19, P3.1 TxD (also matrix column 2) | 1 kΩ in series, then 2 kΩ to GND (5 V → 3.3 V) | GPIO3 (RX) |
| pin 16, GND | – | GND |
| – (own 3.3 V regulator, not the clock's AMS1117) | 220–470 µF + 100 nF | VCC |

- **1 kΩ towards RxD:** outside the listening pauses the clock drives P3.0 hard
  low as matrix column 1; the resistor limits the current against the ESP's
  output. Larger values raise the low level at the pin (P3.0 has a weak
  internal pull-up during reception); 2 kΩ is not recommended.
- **Flashing does not work through the 1 kΩ** (stcgal "incorrect frame
  start", see [firmware/README.md](../firmware/README.md)): flash with the
  adapter clamped directly on pin 18.
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
  character and `\n` in one go (no pause per byte needed).
- Every command ends with "command -> reply" in "Letzte Antwort", also
  `(keine Antwort)`, `(gesendet)`, `pruef` and `zu lang`.
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

Names are German, like the clock's user interface. The select options use the
same names as the clock's own menu (`W_*` tables in `firmware/uhr2.c`).

| Entity | Command |
|---|---|
| Lauftext (scrolling text) | `l…`, at most 253 bytes |
| Helligkeit (Auto, 1–8) | `H0`…`H8` |
| Countdown Minuten + Sekunden, Buttons Countdown starten / stoppen | `CMMSS` / `C0000` |
| Alarm, Alarm-Zeit, Alarm-Melodie, Alarm-Rätsel | `aHHMMx`, `N1`…`N5`, `U0`…`U2` |
| Nachtmodus, Lichtorgel | `Q0`/`Q1`, `O0`/`O1` |
| Animation, Ziffern-Effekt, Doppelpunkt-Effekt, Info-Rotation, Theme, Rück-LEDs, Schrift | `Vw`, `Vz`, `Vd`, `Vi`, `Vt`, `Vl`, `Vs` |
| Buttons: Zeit stellen, Testalarm, Display aus, Melodie probehören, Lichtorgel-Welle | `s…` + `D…`, `U9`, `Q2`, `M…`, `O2` |
| Temperatur | `T` every 5 min |
| Lichtsensor (raw, high = dark), Helligkeitsstufe, Mikrofon-Spitze, Status roh (v) | `v` every 60 s (format in [PROTOCOL.md](PROTOCOL.md#reply-to-v)); the mic peak is the highest value since the previous `v` |
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

## Known problems (measured 2026-11-14)

How the numbers were taken: `v` reports the highest microphone value since the
previous `v` (the ESP polls every 60 s, sensor "Mikrofon-Spitze"). The clock
only samples the microphone while the display is dark (`Q2`, night mode),
while the light organ runs and with rear LEDs on "Ton"; otherwise the peak
stays 0 and means nothing. Old reference without ESP: dark and quiet 7–9,
clapping 47–59.

### `Q2` / night mode wakes up by itself – ESP supply

The dark display wakes on two microphone values above 20 within 10 frames
(about 56 ms).

| Setup, display dark (`Q2`), room quiet | Peaks per minute | Woken by microphone |
|---|---|---|
| ESP on the clock's AMS1117 | 21–33 | 2× in 5.5 min (another run: 4 of 4 `Q2`) |
| ESP on its own 3.3 V supply, GND shared | 12–19 | 0× in 6 min |
| same, ESP 20–30 cm away from the clock | 13–23 | 0× in 6 min |

The path is the 3.3 V supply, not radio: distance changes nothing. The first
minute after a command is the highest each time (the clock replies, the ESP
reports to Home Assistant). With its own supply the margin to the threshold is
small but enough. Not measured yet: the clock without any ESP today (FT232R
only), to see whether the remaining 12–14 is the board itself.

### Light organ fires constantly – rear LEDs

| Light organ on, room quiet | Peak per minute |
|---|---|
| rear LEDs off | 30 (threshold is "above 30": no wave) |
| rear LEDs constant | 4095 |
| rear LEDs breathing | 4095 – the waves visibly "breathe" along |
| rear LEDs "Ton" (2026-11-13, ESP still on the AMS1117) | 4095 for minutes |

Lit rear LEDs (P2.7) drive the microphone input (P1.7, ADC 7) to full scale.
This is electrical, not acoustic and not the ESP: it also happens with the
LEDs constantly on, where P2.7 does not switch, so the LED current itself
shifts the microphone signal (shared ground or supply of the microphone
amplifier, presumably). The digital twin shows no waves without sound, so the
firmware logic is fine. Possibly related: the light sensor (P1.6) reads
higher ("darker") with the rear LEDs on.

Not affected: the normal clock display (the microphone is not sampled there)
and night mode with rear LEDs off.

**Follow-up 2026-11-14, without ESP / with ESP on its own supply** (light
organ on, 30 s per mode, room quiet):

| Rear LEDs | no ESP | ESP only on GND | ESP on its own 3.3 V |
|---|---|---|---|
| off | 8 | 6 | 14–25 |
| constant | 24 | – (clap during the run) | 30, then 4095 after one wave |
| breathing | 23 | 21 | 4095 |

The 4095 is not a steady state but a **feedback chain**: a lit wave adds its
own matrix current, together with the rear LEDs the microphone goes over the
threshold, and that triggers the next wave. Triggered without sound (`O2`):
rear LEDs off → the wave runs out (19–25); constant without ESP → one extra
wave (72), then back to 21; constant with a running ESP → endless (4095).
Dark display (`Q2`) without ESP: 7–9, also with only GND connected (5–7);
with the ESP running on its own supply 16.

**Fixed in the firmware (2026-11-14):** rear LEDs dark while the light organ
runs, 0.5 s dead time after every wave, threshold adjustable (`Vm2`…`Vm9`,
menu "Mikro", default 50). Checked on the clock without ESP: rear LEDs
"constant" + `O2` → 9 in silence, 25 for the wave, then 9 again.

**With the ESP fully connected (own supply, RX/TX through the resistor
network)** the rear LEDs are not the only cause: the ESP alone raises the
quiet level in the light organ to 30, the rear LEDs are dark.

| ESP fully connected, light organ | threshold 30 | threshold 50 |
|---|---|---|
| quiet | 30 | 31 |
| one wave (`O2`) | 4095, chain | 46, one wave |
| one clap | – | 148, one wave |

Only GND connected (ESP on its own supply): quiet 11, wave 22. So the extra
comes through the signal lines. The dead time only slows a chain down (at most
two waves per second), the threshold breaks it; hence the default of 50. The
margin is small (46 vs. 50); which line is responsible (RX or TX) has not
been measured yet.

**Rear LEDs "Ton" with the ESP fully connected** (threshold 50, i.e. 50 +
level/2 while the LEDs are lit):

| Window of one `v` | microphone peak | rear LEDs |
|---|---|---|
| quiet, contains the `Vl2` command | 4095 | quiet |
| quiet, no command but `v` | 77 | quiet |
| one clap | 4095 | one flash, decays quickly |

Single peaks do not matter here: "Ton" only flashes with at least 3 loud
values in one frame (the light organ needs 2 within 10 frames). No chain
after the flash. Where the silent peaks come from is not measured; the ESP's
sync bytes during a command are the obvious suspect.

### Smaller points

- **`O2` does nothing unless the light organ is on** (the firmware only fires
  a wave then; reply `orgel 0`).
- **A command sent while the clock is busy is lost.** It is acknowledged with
  `!` but dropped, e.g. `C0000` right when a countdown ends, or any command
  during a melody (`M…`, `U9`). The ESP retries three times within about
  0.5 s, which is too short.
- **Home Assistant does not resend an unchanged text.** Sending the same raw
  command twice through "Befehl (roh)" only reaches the clock once; send
  something else in between. Buttons ("Display aus") do not have this problem.

### What can be done

| Problem | Hardware | Firmware | ESP only |
|---|---|---|---|
| `Q2` wakes | own ESP supply (done in the test setup) | higher threshold or more hits | – |
| light organ / rear LEDs | series resistor for the rear LEDs, RC filter on the microphone supply | ✅ rear LEDs dark in the light organ, dead time after each wave, threshold `Vm` | select "Mikro-Schwelle" |
| `O2` | – | `O2` also without the light organ | send `O1`, `O2`, `O0` |
| busy | – | reply `#` instead of `!` while busy | wait about 9 s after `M…`/`U9` |

## Not included on purpose

`R` and `u` (bootloader/update), `h`/`w` (halt with a different framing) and
the diagnostic commands. Raw commands starting with `a` set the alarm – set it
again in Home Assistant if that happens while testing.

## Flashing the clock through the ESP (idea, not built)

Until one of these exists, flash the clock with a USB serial adapter (see
[firmware/README.md](../firmware/README.md)). The STC bootloader is in ROM, so
a failed attempt never bricks the clock: the USB adapter always recovers it,
and the EEPROM (settings, calendar) stays untouched.

1. **TCP bridge (less work, recommended first):** a "flash mode" in the ESP
   sends `R`, switches its UART to 115 200 baud 8E1 and exposes it as a TCP
   bridge (external component `stream_server`). On the PC, socat creates a
   virtual serial port for stcgal, started first and without auto reset so it
   keeps sending sync bytes; handshake and transfer both at 115 200 via
   `firmware/stcgal_pause.py`. Open: whether ESPHome can change baud rate and
   parity of UART0 at run time on the ESP8266, and whether the frequency
   trimming tolerates the Wi-Fi delay.
2. **ESP flashes on its own:** upload `uhr2.hex` (about 31 KB) to the ESP or
   let it fetch the file from a URL; the ESP runs the STC8 bootloader protocol
   itself (handshake, frequency trimming, erase, write, options). No PC and no
   Wi-Fi timing, but stcgal's protocol has to be rewritten as an ESPHome C++
   component, and the ESP8285 has only about 45 KB of free RAM.
