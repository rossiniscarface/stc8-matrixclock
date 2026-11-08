# Digital twin

`firmware/uhr2.c` is compiled with gcc for the PC (`-DZWILLING`) and runs from
a script ("Drehbuch"). At the hardware spots the firmware has an
`#ifdef ZWILLING` branch that hands over to `zwilling.c`: display timing,
DS3231 (with running time and alarm), touch keys, microphone, light sensor,
EEPROM. Menu, display, alarm and animations are the same logic as on the clock.

```bash
./bau.sh                                    # -> uhr2_pc
ZW_SKRIPT=drehbuch/menue.txt ZW_AUSGABE=laeufe/menue.jsonl ./uhr2_pc
python3 betrachter.py                       # all runs -> betrachter.html
```

All scripts at once:

```bash
mkdir -p laeufe
for d in drehbuch/*.txt; do n=$(basename "$d" .txt); ZW_SKRIPT=$d ZW_AUSGABE=laeufe/$n.jsonl ./uhr2_pc; done
python3 betrachter.py
```

## Script

One instruction per line, time in seconds from start. The keywords are German:

```
zeit 07:59:50          start time of the DS3231
datum 08.11.26 7       start date and weekday (1 = Monday)
licht 3650             light sensor (ADC 6; dark = high)
@1.0 kreis 0.2         touch circle for 0.2 s   (dreieck = triangle / beide = both likewise)
@5 befehl t2           live command, as via the protocol
@6 laut 0.1            noise for 0.1 s (microphone)
@7 licht 4090          change light sensor
@30 ende               end simulation
```

## Output and viewer

`uhr2_pc` writes JSON lines: image on every change (`"b"`: 25 column bytes
hex), tones (`"ton"`), serial messages (`"uart"`), rear-LED level (`"led"`),
brightness level (`"hell"`). `betrachter.py` combines them with
`betrachter_vorlage.html` into one page: blue square LEDs like the original,
brightness, rear-LED bar, tones (optionally audible), timeline.

Titles, descriptions and groups of the runs are in `betrachter.py`.

## New hardware accesses

Anyone who addresses new hardware in `uhr2.c` or waits for a hardware flag
needs an `#ifdef ZWILLING` branch – otherwise the twin hangs in a wait loop.
