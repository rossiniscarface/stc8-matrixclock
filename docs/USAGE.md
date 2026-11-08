# Usage

Two touch pads on the back: **circle** and **triangle**.

The clock's user interface is in German. Menu items and options are given
below as they appear on the display, with an English translation.

## Normal operation

| Input | Effect |
|---|---|
| triangle short | next screen: time → date → weekday → time |
| circle short | rear LEDs off/on (the last selected effect) |
| both for about 2 s | open the menu |
| circle while a countdown runs | abort the countdown |
| touch during scrolling text / light organ | end it |
| touch in night mode (display off) | switch the display on for 15 s |

The time stays for the configured info interval, then date and weekday follow
for about 3 s each and – once per hour – the day's calendar entries as
scrolling text.

## Menu

Triangle: next item. Circle: change the value or open the item. The current
value is shown in plain text ("Info: 30 s"); while stepping through, only the
new option appears, after 3 s again with its name. After **1 minute without
input** the clock returns to the time display – leaving sub-items without
saving.

| Item (display) | Meaning | Values / operation |
|---|---|---|
| Zeit | time | hours, minutes, seconds: triangle increments, circle next field |
| Datum | date | day, month, year as above |
| Alarm | alarm | an/aus (on/off), then HH:MM |
| Klang | sound | alarm melody: Pip, Steigend (rising), Triller (trill), T3 1 kHz, T3 520 Hz (plays briefly) |
| Quiz | alarm puzzle | aus (off), Code (symbol sequence), Rechnen (arithmetic) |
| Timer | countdown | tap triangle +1 s, holding counts faster (seconds, 10 s, minutes); circle starts |
| Thema | theme | H:M:S (narrow), HH:MM (wide), HH:MM s (small seconds) |
| Schrift | font | 1, 2, 3 – preview on the time, circle saves. 2 = thin, 1 and 3 = bold (digits rounded or square). Also applies to menu and scrolling text |
| Anim | transition animation | aus, Schieben (push), Pixel, Spalten (columns), Oben (top), Mitte (centre), Rollen (roll), Auf-Ab (up-down), Wischen (wipe) – demonstrated when selected |
| Ziffern | digit effect | aus, Aufbau (build), Fallen (fall), Pixel, Auf-Ab, Mitte |
| Punkt | colon | Mitte (centre), Blenden (fade), Konstant, Auf-Ab, Sekunde (every second) |
| Info | info rotation | every 5 s, 10 s, 30 s, 1 min, 2 min or aus |
| Hell | brightness | Auto (light sensor) or fixed 1–8 |
| LED | rear LEDs | aus, konstant, Ton (follow noises), Atmen (breathing) |
| Ton | key beep | an/aus |
| Nacht | night mode | an/aus |
| Orgel | light organ | start it (touching ends it) |
| Update | firmware update | waits for new firmware (display dark, rear LEDs on) |
| Ende | end | leave the menu |

All settings are stored in EEPROM and survive power-off and firmware updates.

## Alarm

The melody plays until the first touch. With Quiz "aus" the alarm is then off.
Otherwise the puzzle appears:

- **Code:** repeat six symbols (○ circle, ▲ triangle). A mistake makes the
  matrix flash, then a new sequence follows.
- **Rechnen:** plus or minus problem with a one-digit result. The triangle
  counts the blinking answer through 0–9, the circle confirms. Wrong: it rings
  again.

20 s without input: it rings again. After 10 minutes it stops in any case.

## Night mode

If it is on and the room has been dark for about 10 s, the display goes off.
Clapping, a loud noise or a touch switch it on for 15 s. When the light comes
on, it continues normally.

## Brightness

Automatic from the light sensor in 8 levels (duty cycle 2 % to 91 %), with a
margin around each level boundary so that the display does not jump back and
forth.
