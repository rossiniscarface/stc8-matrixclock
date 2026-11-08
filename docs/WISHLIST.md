# Wish list (feature comparison with the original, agreed on 2026-11-08)

Basis: the product description of the Geekcreit DS3231 kit, gone through point
by point. ✅ = present, ➕ = wanted, ✖ = deliberately not.

## Already present

- ✅ Set time and date (menu, live `s`/`D`)
- ✅ Alarm with 5 melodies (including 520 Hz T3)
- ✅ Brightness automatic from the light sensor, 8 levels (2–91 %)
- ✅ Night mode: dark → display off, clapping/touch wakes it for 15 s
- ✅ Theme 1 (H:M:S, 3×7), theme 2 (HH:MM, 5×7, colon blinking every second)
- ✅ Extras without a counterpart in the original: scrolling text, live
  commands via UART (ESP), flashing without unplugging

## Wanted (status: ✅ built, viewable in the twin; ⏳ draft/open)

1. ✅ **Display mode HH:MM with small seconds** (in addition to themes 1 and 2)
2. ✅ **Info rotation**: show date, temperature and **weekday** automatically;
   interval in the menu: 5, 10, 30, 60, 120 s or off
3. ✅ **Transition animations as in the original** (10, selectable in the menu):
   time only, scroll left, random pixels, one after another, top to bottom,
   split from the centre, roll centre, roll up/down, wipe sideways, direct
4. ✅ **Digit effects as in the original** (6, selectable in the menu): direct,
   build individually, fall, random pixels, up/down, from the centre
5. ✅ **Colon effects** (several, selectable in the menu)
6. ✅ **More fonts** (3×7: A/B, 5×7: 1/2/3, text thin/bold)
7. ✅ **Calendar via the ESP** (replaces the birthday and holiday reminder)
   - Storage: EEPROM, 64 bytes per entry, text up to ~55 characters, about 500
     entries, survives power-off. stcgal must no longer erase the EEPROM when
     flashing (`eeprom_erase_enabled` off).
   - All-day entries: the text runs as scrolling text in the info rotation on
     that day.
   - Appointments with a time: scrolling text exactly then, with tone.
   - Sync via the live protocol (commands for writing, deleting, listing).
8. ✅ **Countdown**: minutes and seconds in the menu and via ESP, buzzer at the
   end, touching ends it
9. ✅ **Brightness in the menu**: AUTO or fixed level 1–8
10. ✅ **Rear-LED effects**: off, constant, follows the sound, breathing
11. ✅ **Alarm puzzle**, selectable in the menu: repeat 6 symbols of circle/
    triangle, arithmetic problem, or off. Rings until solved; **emergency stop
    after 10 minutes**.
12. ✅ **Light organ mode**: matrix black; a noise triggers a wave animation
    that runs from the centre outwards - like a stone dropping into water.
    The wave always runs through completely, regardless of the volume.
    Technical catch: the microphone is only quiet with the matrix dark (rest
    up to 9); lit LEDs couple in up to 20. Listening therefore happens in the
    dark time slots or with a threshold above the crosstalk level.

## Decisions at acceptance (2026-11-08)

- Fonts: 3×7 A square and B rounded (FONT 1/2), 5×7 style 1 photo, 2 thin,
  3 square bold (FONT 1/2/3). 7-segment dropped.
- Theme 3: minutes one column to the right, colon free.
- Weekday: Mon, Die, Mit, Don, Fre, Sam, Son (German abbreviations) in the
  5×7 font.
- Info rotation: date → weekday → temperature, 3 s each.
- Defaults: animation 2 (random pixels), digits direct, colon every second.
- Arithmetic puzzle: only plus/minus with a one-digit result; triangle counts
  0–9.
- Countdown: circle aborts.
- Calendar: all-day entries once per hour.
- Rear LEDs constant: 100 %.
- Menu INFO shows 1–5 (5/10/30/60/120 s) – the seconds do not fit into the
  columns.
- Twin: blue square LEDs like the original.

- Temperature not in the info rotation (measures the chip, not the room);
  available via `T`.
- Info has an option "aus" (off); Anim "aus" just means without animation.
- Font also applies to text: 2 thin, 1 and 3 bold.
- Menu in 5×7 with plain text, return after 1 min, timer by holding.

## Deliberately not

- ✖ 12 h mode, MM-DD, °F (stays 24 h, DD.MM, °C)
- ✖ Fixed holiday list (covered by the calendar)
