# Development notes

Notes that grew chronologically: measurements, bug hunts and decisions, with the
why. **Older sections describe earlier states** (such as the first menu or
57 600 baud); the current state is at the end ("State 2026-11-08") and in
[USAGE.md](USAGE.md) and [PROTOCOL.md](PROTOCOL.md).

Menu texts and replies of the clock are German; they are quoted here as they
appear on the clock.

`firmware/uhr2.c` is the application, everything else is measurement tools from
the reverse engineering (see [PINOUT.md](PINOUT.md)).

## Operation

| Input | Effect |
|---|---|
| triangle short | change the display: time, date, temperature |
| circle short | rear LEDs on/off |
| both 2 s | open the menu |
| in the menu: triangle | next item — ZEIT, DATUM, ALARM, TON, LED, THEMA, UPDATE, ENDE |
| in the menu: circle | select the item; for TON, LED and THEMA it toggles |
| in the menu: circle | select the item |
| in the editor: triangle | increase the value |
| in the editor: circle | next field; after the third it is saved |

Two seconds for the menu are intentional. With half a second, a false trigger
of the touch keys started the setting mode and silently changed the time.

## Display: Theme1

Two themes, switchable in the menu under THEMA. The choice lives in register
0x0B of the clock (alarm 2, minutes; alarm 2 is disabled) and thus survives a
power failure. Proven after a restart via the bootloader; a real power cycle
is not tested.

**Theme1** — accepted by the user on 2026-11-07: `hh:mm:ss` in 3x7.

**Theme2** — after the product photo: `hh:mm` in 5x7 without seconds,
5+1+5+1+1+1+5+1+5 = 25 columns, colon in column 13 (rows 2 and 6).
Read off two product photos are the digits **1, 2, 3, 5, 6** and the colon
(two dots of two LEDs height each). 3, 5 and 6 appear in both photos and are
identical there; that confirms the reading independently. The 2 has a full
middle bar (5 LEDs) and a bottom bar starting at column 2.
`host/glyphen_pruefen.py` shows "16:35" and "23:56" and compares the frame
buffer cell by cell with the patterns read off — 0 of 25 columns differ. The
expected values are in the script, not in the firmware table, otherwise it
would confirm itself.

**0, 4, 7, 8, 9** and the C do not appear in the photos. They are built in the
same style (verticals 2 LEDs wide, horizontals 1 high; the 9 is the 6 rotated
by 180 degrees) and confirmed as fitting by the user. How the original draws
them is unknown. Date and temperature appear in both themes in the respective
font; the editor in the menu always shows the three-field Theme1 layout,
because it needs hour, minute and second.

### Theme1

`hh:mm:ss`, all six digits in the 3x7 font, two colons.
Column layout (1 = left, as specified by the user):

| Columns | Content |
|---|---|
| 1-3 | hours, tens |
| 4 | blank |
| 5-7 | hours, units |
| 8 | colon |
| 9 | blank |
| 10-12 / 14-16 | minutes, with blank column 13 in between |
| 17 | colon |
| 18 | blank |
| 19-21 / 23-25 | seconds, with blank column 22 in between |

That is exactly 25 columns with nothing left over. Why not the usual 5x7
font: six digits would be 30 columns wide in it.

Brightness follows the light sensor in eight levels and is tracked slowly so
that it does not flicker in changing light. The lowest level is at 40 % duty
cycle — below that nothing is visible anymore with 1/25 multiplexing.

## Menu feedback

For ALARM, TON, LED and THEMA a digit to the right of the text shows the
current state: 1 = on, 0 = off, for the theme its number. It changes
immediately when toggling. Before, there was no feedback there; one only saw
after leaving the menu whether anything had happened ("THEMA does not work").

The menu opens after holding both keys for about 2 s (19 polls, calculated at
5.2 ms per frame and 2.4 ms key measurement). With the old frame duration it
was about 4 s; reported was "a bit longer than 2 s".

## Alarm

**Operation:** menu item ALARM first shows `AN` or `AUS` (on/off; triangle
toggles, circle confirms); with `AN` the time follows as `hh:mm` in the large
font, the selected field blinks. Before, the alarm went through the three-field
editor with on/off as the first field and appeared as "01:07:30" — like a date.

**Bug, fixed:** the alarm enable was cleared at start-up. `steuer` had the
initial value 0x04 in RAM, and `buzzer(0)` at start-up wrote it into the
control register. So the alarm was off again after every restart, although it
was supposed to survive a power failure. Now the register is read before the
first `buzzer()`; `host/bedienung_pruefen.py` proves this across a restart
(0E stays 05).

Alarm time and enable live in the **alarm registers of the DS3231**
(0x07-0x0A and A1IE in 0x0E), not in the controller. So they survive a power
failure. The firmware only checks the A1F flag and lets the buzzer ring every
second until a key comes or about a minute is up.

The buzzer hangs off the clock chip's square-wave output, not the controller.
When switching it on and off, the alarm's enable bit must therefore be
preserved — otherwise the alarm would be off after every key beep.

## Serial port and uploading

P3.0 and P3.1 are both matrix column and UART. Scanning the other columns
disturbs reception considerably: **with the display running, a single
character arrived in only 38 % of cases**, even with P3.0/P3.1 as dark columns
(measured, 40 attempts). With the display dark every character arrives (40 of
40). Probably ground offset from the LED currents or coupling onto the thin
clamp leads; the cause is not resolved.

**Listening window in normal operation:** 2 frames every 180 (about 8 ms every
0.7 s), only P3.0 dark. Before, 200 of 400 frames and two columns, so the
display blinked constantly; now it is about 0.04 % of the column time. The
first character in the window halts the firmware. Because it hits so rarely,
the sender must **send without gaps**: `host/anhalten.py` sends dots
continuously for a second and then checks with `d`. Measured: 5 of 5 in 2.2 s,
no halting by phantom characters over 25 s.

**Menu item UPDATE** (fallback for uploading): the text is shifted two columns
to the right, P3.0 and P3.1 explicitly stay dark. After 3 s the matrix goes
completely off and the rear LEDs stay lit, then listening is reliable. A
single `R` starts the bootloader immediately (5 of 5), circle or triangle end
the mode. Way to flash without scripts: menu -> UPDATE ->
`stcgal ... -r ./isp_reset.sh`.

In the halted state:

| Command | Effect |
|---|---|
| `d` | raw registers 0x00-0x06 in hex |
| `aHHMMx` | set alarm, x = 1 on, 0 off |
| `r` | registers 0B (theme), 0E (control), 0F (status) |
| `u` | enter update mode (for testing without keys) |
| `t1` / `t2` | set and save the theme |
| `zHHMM` | put Theme2 with arbitrary digits into the frame buffer without changing the time |
| `b` | frame buffer as 50 hex characters (25 columns) |
| `sHHMMSS` | set time |
| `w` | continue |
| `R` | restart into the bootloader |

## Compiling

`./build.sh uhr2.c` — with `--model-large` since the menu. The 8051 has only
128 bytes of directly addressable memory; the fonts alone need 190. All tables
are additionally placed explicitly in program memory (`__code`), otherwise they
still end up in data memory.

**Pitfall:** empty delay loops need a `volatile` counter. Without it the
compiler throws them away, and the display stays dark without anything looking
like an error.

## Not yet tested

The clock was lying on its display during development, so there is **no image
for checking**. Thus untested are:

* the look of both fonts — they are only designed on paper
* the menu texts
* whether the touch threshold (40 above an idle value of about 290) fits in
  operation, i.e. neither swallows touches nor triggers by itself
* the alarm as a whole

Tested, on the other hand, is that the firmware runs, reads the clock
correctly (raw registers against the PC time), the light sensor regulates and
19 columns of the image are used.

## Not implemented

Missing from the product description: 12/24-hour switching, font selection,
display animations, birthday mode, countdown and automatic switching of the
screens. These are convenience features; the basic functions are in place.

## Checking the layout without a camera

`host/theme_pruefen.py [1|2]` halts the clock, sets the theme, reads the frame
buffer via `b` and redraws it as 7 rows with grid marks. That works without a
camera and is therefore the reliable check as long as the ESP32 camera's
exposure does not suit the setup.

A trap: the message "angehalten" (halted) only comes at the transition. A
stray character during boot may already have halted the firmware, and then
one waits for it in vain. The script therefore asks with `d`, which is only
answered in the halted state.

A second trap when halting: while the display runs, the firmware drives P3.1
as a matrix column and produces garbage that is long enough to fool a merely
length-based check ("did a reply come?"). `glyphen_pruefen.py` therefore only
accepts a reply in the exact register format.

## Font and scrolling text

`host/schrift_art.py` is the **template**: 105 characters as ASCII patterns,
5 columns by 7 rows. `host/schrift_erzeugen.py` turns it into
`firmware/schrift.h` (525 bytes in program memory). Changes are made only in
the template.

**Character set:** ASCII 0x20-0x7E (capitals and lowercase, digits, all
punctuation) plus Ä Ö Ü ä ö ü ß, degree, euro and section sign. Text comes as
UTF-8; everything unknown becomes exactly one `?`, even if it is several bytes
long (an emoji does not give four question marks).

**Structure of the seven rows:** capitals and digits at full height, lowercase
5 high (rows 3-7), ascenders up to row 1. With seven rows there is no room for
descenders: for g j p q y the bowl sits in rows 3-5 and the tail runs down to
row 7, so these letters have a flatter bowl than their neighbours. Capitals
with umlaut have a body squashed by one row and dots in the top row.

**Variable width:** empty edge columns are trimmed, an empty column follows
each character, the space is three columns wide (plus gap).

**Scrolling text:** text -> column stream (up to 1200 columns, text up to
256 bytes) -> 25-column window that moves on column by column. The text enters
on the right and leaves the display on the left. During the run the UART is
dead, P3.0/P3.1 are columns.

**Speed:** 14 frames per column, measured 14.4 columns/s. Because the frame
period is now fixed by the timer (see below), the speed no longer depends on
other code. Before, the main loop's frame time determined the speed, and 16.0
became 12.8 columns/s after unrelated changes.

Commands in the halted state:

| Command | Effect |
|---|---|
| `lText\n` | run the text once as scrolling text, then `fertig` (done) |
| `LText\n` | only load, reply `ok <number of columns>` |
| `S` | loaded column stream as hex |
| `WNNNN` | window at step NNNN into the frame buffer (then `b`) |

**Tested:** `host/laufschrift_pruefen.py` computes the expected stream
independently from the patterns and compares it byte by byte with the
firmware, plus four scroll windows each — pangram, capitals, lowercase,
digits, all punctuation, umlauts, a German sentence, unknown characters,
multiple spaces: 9 of 9 without difference. `host/laufschrift_show.py` runs
texts for watching. **Not tested** is the look on the display itself; the
firmware's behaviour is proven, whether the characters look nice is not.

**Not yet built in:** the scrolling text is reachable via the serial port, but
not yet in the menu and not part of the clock display.

After ringing, the firmware sends `wecker` (alarm) via the UART (the display
resumes right away), so that the ringing can be proven without listening. The
ringing itself is **not tested**.

## Display in the interrupt (against flicker)

**Finding:** the whole display flickered in clock operation, but not when only
the scanning ran (test with all LEDs on, with and without P3.0/P3.1). The rear
LEDs flickered too. Measured, the scanning stood still: key measurement
**2.4 ms** every 20 frames, reading the clock **3.0 ms** every 100, with a
frame of 4.7 ms. During that time the display is dark, about 10 times per
second.

**Solution:** timer 0 scans in the interrupt, two events per column (on, off
after the on-time). The main loop draws into `bild`, `rahmen()` copies
atomically to `scan` and waits for the end of a frame. Brightness is the
on-time, the period stays the same.

**Proven:** with `F1` (100 times key measurement plus reading the clock with
the display running) the interrupt delivers 118 frames in 616 ms; at 5.2 ms
frame period one expects 119. Without tasks also 5.2 ms per frame. So the
scanning runs through.

**Consequences:**
* Frame period 5.2 ms instead of 4.7 ms (about 190 Hz).
* **Rear LEDs** now light with 25 % duty cycle (every fourth column), before
  about 2 %. They are therefore noticeably brighter.
* The key measurement disables the interrupt per repetition (about 70 us),
  because the interrupt changes the same port registers.
* For the UART, `anzeige_aus()` applies instead of `ports_all_highz()`:
  otherwise the interrupt drives the columns again at the next time slot.
* The ten-millisecond tick for the dark update mode comes from timer 1.
* Test command `F0`/`F1` in the halted state.

## Commands during operation (listening pauses, 921600 baud)

**Why reception failed with the display lit:** the LEDs of column 1 sit
between the rows and P3.0 (RxD). In every on-phase the lit rows feed current
into the receive line through these LEDs - no matter which column is active.
Measured: display dark 100 %, 1-3 rows about 78 %, all 7 rows practically
nothing. Switching off column 1 alone does not help.

**Solution:** short listening pauses in which all rows are low: 80 us after
every 5th column (5 per frame, at most 1.1 ms apart). Evenly across all
columns, about 7 % darker, no flicker (confirmed by visual test).

- **921600 baud** (divider 5 from 18.432 MHz, exact; FT232R 0.16 % off).
  Measured rates (first attempt, clock display / all LEDs): up to 230400
  30/30, 921600 28/30 / 30/30, 1152000 26/30 / 30/30, 1536000 27/30 / 29/30,
  2304000 0 (the FT232R cannot hit it). For measuring: `B0`..`B7` switches,
  without a valid line it returns to 921600 after 3 s.
- Sender: 150 x `0xFF` (1.6 ms, longer than the pause interval), command,
  check character `0x40 + (sum & 0x3F)`, `\n` (`host/live.py`).
- Three `0xFF` in one pause = sync: the matrix stays dark, the clock sends
  `!`, the line arrives undisturbed. As soon as it is complete, the display
  continues; the command is executed with the display lit, the reply buffered
  and sent in a short dark moment (TxD is column 2).
- Measured: **dark about 1.5 ms per command** (before 10-25 ms); 50/50
  commands on the 1st attempt, 49/50 with all LEDs, 15/15 and 19/19 during
  scrolling text, 0 checksum errors. An ESP that stops the 0xFF sequence on
  `!` gets off even shorter.
- Image takeover in the timer interrupt (not with EA = 0), UART interrupt with
  priority: at 921600 baud only 11 us remain between two characters.
- Holding ends 2 frames after the last character, at the latest after about
  100 ms.
- Scrolling text (`l`) runs in operation in the main loop, non-blocking;
  commands keep working meanwhile, touching aborts.
- Commands: `d` `r` `t1`/`t2` `s` (HHMMSS) `D` (TTMMJJW, W 1=Mon..7=Sun) `a`
  `l`/`L` `z` `b` `B` `x` (characters, syncs, lines, errors, dark time slots)
  `k0..k7`/`K` (test pattern), `h` halt (then raw, `w` continue), `R`
  bootloader.

**Flashing:** `firmware/flash.sh` (isp_reset.sh sends the protocol with `R`,
921600 baud). Aborts only while stcgal is still waiting, never in the middle
of writing. Most recently 3/3 on the first attempt. Always pass the port with
`-p`: as a positional argument stcgal reads it as the EEPROM file and opens
the port a second time.

**ESP:** 921600 baud, TX via about 1 kOhm to P3.0 (column 1 otherwise pulls
against the output there). Replies contain garbage bytes from the display:
only evaluate printable lines after the `!`.

## Initial values: SDCC startup routine and STC8

SDCC copies the initial values of initialised XRAM variables (crtxinit) with
`MOVX @R0` and P2 as the high address byte. The STC8 does not support that -
the values never arrived: after a cold start random, after a bootloader start
leftovers of the previous firmware. Consequence: the display stayed dark after
power-off. `__sdcc_external_startup()` in uhr2.c now copies them itself via
DPTR. **Applies to every SDCC firmware on the STC8 with `--model-large`.**

## Alarm and melodies

- **No click:** the buzzer hangs off INT/SQW of the DS3231 via Q1. With the
  alarm interrupt (A1IE) the chip pulled the pin low at the alarm time until
  the firmware reacted - that clicked. A1IE now stays off, the firmware polls
  the A1F flag. The alarm enable lives in register 0x0A bit 0 (with A1M4 = 1
  the DS3231 ignores the day bits).
- **Melodies** (choice in 0x0C, menu KLANG, live `N1`..`N5`, play `M1`..`M5`):
  1. classic pip pip ... (4.096 kHz)
  2. rising 1.024 / 4.096 / 8.192 kHz
  3. trill 4/8 kHz like a telephone
  4. T3 pattern like a smoke alarm (3 x 0.5 s tone/0.5 s pause, 1.5 s pause),
     1.024 kHz
  5. T3 pattern with real 520 Hz - background: according to the studies of the
     group around D. Bruck, low 520 Hz square-wave tones wake deep sleepers
     (including children and teenagers) considerably better than high beeps;
     NFPA 72 requires them for sleeping rooms. The DS3231 cannot do 520 Hz: in
     1 Hz mode its output is low for 0.5 s at a time; during that the firmware
     toggles 1040 times per second between 1 Hz mode and INTCN (timer 1, I2C
     without wait loop). Measured: not a single toggle too late. Loudness on
     the small buzzer: open.
- Ringing duration about one minute, touching ends it.

## Brightness

- **Levels** (duty cycle per column): 2 %, 6 %, 10 %, 23 %, 35 %, 51 %, 71 %,
  91 % - accepted by the user in a sweep with all pixels. Before linear
  40-87 %: far too bright at night, differences hardly visible. More than
  about 91 % is impossible (time for the switch-off interrupt); the STC8A has
  no stronger driver stage (PxDR) - checked, the register is missing, P2PU on
  the other hand can be read/written.
- **Light sensor** P1.6, ADC channel 6: dark = high value. Measured: covered
  4090, room light at night 3670, flashlight 770. Curve on 4095 - value,
  thresholds 30/60/120/240/480/960/1920 (each level twice as much light); room
  light gives level 5 (chosen so by the user). Every 0.5 s one level towards
  the target.
- **Flicker at low levels** came from the touch measurement: 32 x 70 us with
  EA = 0 right after the start of the frame delayed the switch-off interrupts
  of the first ten columns. Now about 15 us per measurement and only in a long
  timer interval (`ruhefenster()`); idle values unchanged and stable.
- Commands: `H1`..`H8` fixed level, `H0` automatic; `v` sensor value, level,
  touch.

## Night mode (clapping wakes)

- Setting: menu NACHT (0/1), live `Q0`/`Q1`, stored in 0x0D. `Q2` switches the
  display off immediately, until the next clap or touch.
- If the room is dark for 10 s (light value below 100), the display goes off.
  Clapping, noise or touch: on for 15 s. Light on (light value above 200):
  normal. Alarm and scrolling text wake it as well. Measured: room light 450,
  covered 5-60 (the clock's own display shines onto the sensor through the
  finger).
- Microphone P1.7 (ADC 7), only sampled in the dark state (otherwise the LEDs
  couple in up to 20): quiet at most 7-9, clapping 47-59. Wake-up with at
  least 2 values above 20 within 10 frames. 60 s quiet test: no false wake-up,
  peak 7-8.
- The listening pauses continue in the dark too; commands arrive (50/50).
- `v` additionally shows: setting/dark/wake frames, wake-up reasons
  microphone/touch and the microphone peak since the last query.

## State 2026-11-08: menu, display, calendar (overview)

**Files:** `firmware/uhr2.c` (main program), `firmware/anzeige.c` (display
sequence: info rotation, transition animations, digit and colon effects,
theme 3, weekday, countdown screen), `firmware/kalender.c` (calendar in
EEPROM). Both are included via `#include`. Digital twin: `zwilling/` (see
there).

**Settings** in EEPROM sector 0 (preserved when flashing): theme, sound,
night, key beep, LED effect, quiz, anim, digits, colon, info, brightness, font.

**Menu** (both keys 2 s; triangle next, circle change/open; return after 1 min
without input, leaving sub-items without saving). Texts in 5x7, long ones
scroll, values in plain text; while stepping only the option, after 3 s again
with its name: Zeit, Datum, Alarm, Klang (Pip/Steigend/Triller/T3 1 kHz/T3
520 Hz), Quiz (aus/Code/Rechnen), Timer (holding the triangle counts up
faster), Thema (H:M:S/HH:MM/HH:MM s), Schrift (preview, circle saves; applies
to digits and text), Anim (aus + 8, demonstrated), Ziffern (aus + 5), Punkt
(5), Info (5 s - 2 min, aus), Hell (Auto, 1-8), LED (aus/konstant/Ton/Atmen),
Ton, Nacht, Orgel, Update, Ende. Translations in [USAGE.md](USAGE.md).

**Display:** info rotation date -> weekday (Mon..Son) -> calendar (once per
hour); temperature only via `T`. Colon "Blenden" (fade) as real PWM in the
interrupt (separate on-time for the separator columns). Automatic brightness
with 12.5 % hysteresis. Rear LEDs with 64 levels (sum with overflow per time
slot). Writing the EEPROM darkens the matrix briefly (the CPU stalls 5 ms
while erasing).

**Alarm:** melody until touch, then puzzle (Code: repeat 6 symbols; Rechnen:
plus/minus one-digit, triangle counts 0-9), 20 s without input -> rings again,
emergency stop after 10 min. Test alarm via `U9`.

**Live commands (selection):** `d` registers, `s`HHMMSS, `D`TTMMJJW, `a`HHMMx
alarm, `t1..3` theme, `l`text scrolling text, `C`MMSS countdown, `T`
temperature, `Y+ Y- Yd Y? Yl` calendar, `V`xn setting (w Anim 0-8, z digits
0-5, d colon 1-5, i info 0-5, t theme, l LED 0-3, h brightness 0-8, s font
1-3), `H`n fixed brightness, `M`n play melody, `N`n choose melody, `U`n
quiz/test alarm, `Q`n night (2 = off immediately), `O`n organ, `E` raw
settings, `v` sensors, `x` counters, `k`/`K` test pattern, `h` halt, `R`
bootloader. Memory: 30.8 of 32 KB.

## Long commands and flashing (2026-11-13)

Found with an ESP8285 (ESPHome) on the serial port: commands longer than
about 60 bytes got lost. Measured with the FT232R, one attempt per line, sent
in one go:

| Length | old firmware | new firmware |
|---|---|---|
| 20–60 bytes | 30/30 | 50/50 |
| 64–254 bytes | 0/30, no reply | 50/50 (also with all LEDs on) |
| 255 / 300 bytes | 0/30 | `zu lang` 50/50 |

**Cause:** the received bytes went into a 64-byte ring buffer that the main
loop only emptied once per frame; while a line came in, the main loop waited
for the end of the frame. Longer lines only got through if a frame end
happened to fall into them – otherwise the rest, often with the `\n`, was lost
without a trace and without a check error.

**Two attempts that were not good enough:** a 256-byte ring emptied in the
wait loops (254-byte lines need 257 places; reading the clock over I2C blocks
the main loop for 3 ms) and a 512-byte ring with 16-bit pointers. Those need
locks: with `ES = 0` the display interrupt could start inside the window and
then could no longer be interrupted by the UART (frame end about 20 µs, one
byte every 10.8 µs) – check errors even at 20 bytes. With `EA = 0` it was
better, but bytes were still lost now and then.

**Solution:** the UART interrupt writes the live line directly into `zeile[]`
(8-bit index, 254 bytes plus check character) and sets `zeile_fertig` on
`\n`; the main loop owns the line until it has executed it. No shared
pointers, no locks, the interrupt is as short as before. Wrong check
character: reply `pruef`; too long: `zu lang`. The small ring buffer is only
used in the halted state (`h`).

**Flashing:** at the same time stcgal failed with "Trimming frequency:
Protocol error: incorrect frame start", via `R` and with a power cycle alike.
Recorded by hand, the bootloader (BSL 7.3.11U) answers the trimming request
correctly – when it comes 0.3 s after the status packet. stcgal sends it
immediately; the bootloader then discards it, starts the old firmware, and
stcgal reads its display garbage (`FE`) as the reply. `firmware/stcgal_pause.py`
inserts the pause; `flash.sh` uses it. Why it worked before without the pause
is not known.

**Supply:** measured by the user, the STC8 runs at 5 V, TxD has 5 V levels; a
3.3 V ESP needs a divider on its RX line. The DS3231 has no working backup
battery: after a power cycle the time is 01.01.00 00:00.
