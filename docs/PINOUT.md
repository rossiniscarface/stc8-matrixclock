# STC8A4K32S2A12 — pinout of the matrix clock

Determined on 2026-11-07 by running through all pin pairs and observing them
with an ESP32 camera: a pair of source (high) and sink (low) lights exactly one
LED, whose position the camera locates.

**All 175 LEDs were measured individually**, not a single one is inferred from
the grid. Brightness of the hits 215–248 against a background of 26.

Everything in this document was determined **in software only** – pins driven
and read by test firmwares, observed via camera, ADC readings and I²C
acknowledgements. Measured with an instrument are only the supply voltages:
the microcontroller runs at 5 V (pin 14), the 5 V rail shows about 4.9 V at the
light sensor, and the AMS1117-3.3 supplies the DS3231.

## The display

**25 x 7 dots, 175 LEDs.** Not an assembly of 8x8 modules but one continuous
grid — the classic size for a 5x7 character font with four digits, colon and
spaces. Grid pitch in the camera image 19.6 px lengthwise and 20.0 px across;
the rows sit on a perfect grid with less than 3.5 px deviation.

## The 7 rows of the display (source, high)

| # | Pin | x in image |
|---|---|---|
| 1 | **P2.0** | 507.7 |
| 2 | **P2.1** | 487.9 |
| 3 | **P2.2** | 468.4 |
| 4 | **P2.3** | 449.0 |
| 5 | **P2.4** | 429.5 |
| 6 | **P2.5** | 410.3 |
| 7 | **P2.6** | 386.4 |

All seven are on port 2, next to each other in pin order.

## The 25 columns of the display (sink, low)

| # | Pin | y in image |
|---|---|---|
| 1 | **P3.0** | 61.6 |
| 2 | **P3.1** | 81.5 |
| 3 | **P3.2** | 101.0 |
| 4 | **P3.3** | 120.5 |
| 5 | **P3.4** | 139.9 |
| 6 | **P3.5** | 160.6 |
| 7 | **P3.6** | 179.8 |
| 8 | **P3.7** | 199.2 |
| 9 | **P0.0** | 218.7 |
| 10 | **P0.1** | 238.4 |
| 11 | **P0.2** | 257.7 |
| 12 | **P0.3** | 277.0 |
| 13 | **P0.4** | 296.6 |
| 14 | **P0.5** | 315.9 |
| 15 | **P0.6** | 335.6 |
| 16 | **P0.7** | 355.2 |
| 17 | **P1.0** | 374.6 |
| 18 | **P1.1** | 394.2 |
| 19 | **P1.2** | 413.6 |
| 20 | **P1.3** | 433.1 |
| 21 | **P1.4** | 454.1 |
| 22 | **P1.5** | 473.9 |
| 23 | **P4.0** | 493.3 |
| 24 | **P4.1** | 513.1 |
| 25 | **P4.2** | 533.2 |

Without gaps in pin order: P3.0–P3.7, P0.0–P0.7, P1.0–P1.5, P4.0–P4.2. The
designer wired them in sequence. That alone made it possible to predict P3.0
and P3.1 (y=62 and 82 expected, 57.4 and 77.4 measured).

## What is not part of the matrix

| Pin | Function | Evidence |
|---|---|---|
| P5.4 | I2C **SCL** to the DS3231 | ACK on address 0x68 |
| P5.5 | I2C **SDA** | likewise |
| P1.6 | **light sensor** (LDR) | flashlight: range x213 |
| P2.7 | **only** the two rear LEDs | light up when clocked |
| P4.3 | **touch pad circle** (set key) | charge time rises when touched |
| P4.4 | **touch pad triangle** (switch key) | likewise |
| P1.7 | **microphone** (ADC7) | buzzer at 1 kHz: +17, control P1.6 only +2 |
| P4.5, P4.6, P4.7 | react to nothing | probably not bonded out |

All these pins were explicitly tested as source **and** as sink against known
partners: 30–48 against 247 in the cross-check. So no other pin of the chip can
drive an LED.

The UART pins (P3.0 RxD, P3.1 TxD; also matrix columns) with photo and pin
numbers: [PROTOCOL.md](PROTOCOL.md#pins-on-the-chip).

## The touch pads - and why they were so hard to find

**P4.3 is the circle, P4.4 the triangle.** Both hang on a pull-up of about
30 kOhm; touching increases the capacitance and thereby measurably lengthens
the charge time.

Four methods failed before, each for its own fundamental reason - that is the
real lesson:

| Method | why it was blind |
|---|---|
| RC discharge time | the pull-up holds the pin high, the measurement saturates |
| charge sharing via 1 MOhm | assumes a series resistor that does not exist here |
| restlessness (50 Hz coupling) | free pins already jitter by 150-650 without a finger |
| ADC charge sharing | P4.x are not analog channels |
| continuity tester on the pin | 30-50 Ohm output resistance is above the beep threshold |

What finally worked: **drive the pin low, release it and count in a tight loop
until the pull-up has pulled it high - and sum that up 128 times.** The charge
time is only one to two microseconds, a single measurement is far too coarse
for that. The resolution does not come from a smaller step but from averaging:
idle values 1152 (P4.3) and 1024 (P4.4), bit-exactly constant over all
measurements.

Also decisive was a **positive control**, which was missing for a long time.
Only at the light sensor P1.6 - whose location was known independently - could
I check whether a method *can* detect anything at all. Without this
touchstone every "nothing found" had two possible causes, and I assumed the
wrong one several times.

## The microphone - proven with a negative control

**P1.7.** The idle level is at 0 V, the restlessness at 4-6 counts, i.e. very
quiet. The buzzer on the board itself served as the sound source: the PC has no
speakers, all analog outputs report "not connected".

The trap: the buzzer draws current and modulates the supply. All channels that
hang on a low-impedance net see that - and those are exactly the two
interesting ones. **P1.6, the light sensor, therefore served as the negative
control:** same supply net, but deaf.

| Tone | P1.6 (control) | P1.7 | excess |
|---|---|---|---|
| silent/silent (blank) | +0 | +0 | +0 |
| 1.024 kHz | +2 | **+17** | **+15** |
| 4.096 kHz | +32 | +27 | -6 |
| 8.192 kHz | +34 | +17 | -16 |

The decisive row is 1.024 kHz: the control stays flat, the candidate jumps.
Supply hum would hit both. At 4 and 8 kHz the electrical interference drowns
out the comparison; nothing can be concluded there.

The blank test with two silent phases gave exactly 0 - so the method has no
systematic bias.

**Independently confirmed** with a tone from the monitor speakers - a source
without any electrical connection to the clock. The effect there is only +2
counts and thus below the noise of a single measurement; it becomes detectable
through alternating tone and silence blocks with a sign test:

| Frequency | P1.7 silent -> tone | hits | P1.6 (control) |
|---|---|---|---|
| 800 Hz | 5.0 -> 5.0 | 2/7 | +0.0 |
| 1500 Hz | 5.0 -> **7.0** | **7/7** | +0.0 |
| 2500 Hz | 5.0 -> **7.0** | **7/7** | +0.0 |
| 3500 Hz | 5.0 -> 5.0 | 0/7 | +0.0 |

A single 7/7 has a chance probability of 1/128, two independent ones together
about 1 in 16 000. The control is at exactly 0.0 everywhere.

Most convincing is the **frequency selectivity**: a measurement artefact or an
electrical coupling would be frequency-independent. Effect at 1500 and
2500 Hz, none at 800 and 3500 Hz - that is the response of speaker, room and
microphone. So the path is necessarily acoustic.

**Methodological note:** the effect is smaller than the noise of a single
measurement. Whoever measures only once finds nothing and wrongly takes that
for a negative result - exactly how the first attempt ended. The resolution
comes from repetition, not from a better single measurement.

## A bug that was in there for a long time

**P4.2 was locked in the scanner as "not bonded out on the LQFP44".** This
assumption came from a pinout, not from a measurement, and kept the pin out of
all 812 pairs of the main run. It was only noticed when an unmarked row of
squares stood out at the lower edge of the image. P4.2 is the 25th column.
Lesson: whatever is locked out must be justified just as well as what is
measured — a lock produces no error message, only a gap that looks like a
result.

## The buzzer does not hang off the controller

It is driven by the **SQW square-wave output of the DS3231**, via Q1 (PNP
9012). Proven on 2026-11-07: setting the clock's control register 0x0E to
0x00 / 0x08 / 0x10 / 0x18 gives 1 Hz (audible clicks of single edges) as well
as 1.024, 4.096 and 8.192 kHz as three rising tones.

Before that, all six free controller pins had been probed with constant level
and sweep tone — silent, and rightly so: the controller does not reach the
buzzer directly at all. It only switches it via I2C, with bit 2 (INTCN) of the
control register, and has exactly four frequencies to choose from.

The hint was in the manual: "If the buzzer is not called, please check whether
DS3231 and 9012 Triodes are soldered." That it makes the CLOCK chip partly
responsible for the tone was the decisive pointer.

## Orientation — resolved

With `muster.c`, "12:34" was displayed and photographed. Rotating the camera
image **counter-clockwise** gives readable text. From this:

* **Column 1 (P3.0) is on the left**, column 25 (P4.2) on the right.
* **Row 1 (P2.0) is at the top**, row 7 (P2.6) at the bottom.
* In the font, bit 0 of a column byte is the top row.

This confirms the mapping end to end: 175 LEDs over 32 pins, multiplexed
column by column, give readable digits.

## Driving the display

`firmware/muster.c` scans column by column: one column low, the desired rows
high, on to the next. Each LED is lit 1/25 of the time and still reaches
brightness 226 of 255 in the camera image.

Multiplexing the other way round - one row sourcing, up to 25 columns sinking -
would be the obvious mistake: more than three times as much current would then
flow through the one row pin. The matrix has no series resistors.

While displaying, the UART is silent, because P3.0 and P3.1 are columns
themselves. Each pattern therefore runs only for a limited time and then
reports back.
