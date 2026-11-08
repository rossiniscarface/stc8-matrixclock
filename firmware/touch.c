/*
 * Find the touch pads - with the buzzer as feedback.
 *
 * Method: drive the pin high briefly, then switch to high impedance and
 * count until it has dropped low. The time depends on R*C. R1/R2 are
 * 1 MOhm, a finger adds about 20 pF - so the count rises when touched.
 *
 * The count makes two things visible along the way:
 *  - Pins without any discharge path run into the upper limit (SATT). That
 *    alone separates the touch pins from the rest.
 *  - Measurement is against an idle value recorded at start-up, only the
 *    deviation is output. A touch is therefore visible immediately, without
 *    having to compare absolute values.
 *
 * The buzzer hangs off the SQW output of the DS3231 (register 0x0E), not
 * the controller - so this firmware beeps via I2C.
 *
 * Commands:  m  measure (runs until the next character)   R  bootloader
 */
#include "stc8.h"

#define DS3231   0xD0
#define OBERGRENZE 3000u
#define SCHWELLE    12          /* from this deviation on it counts as touched */

/* ------------------------------------------------------------------ I2C */

/* Shadow latch: reading P5 directly with |= returns the level at the pin
 * with an open collector, not the intended value — a low held from outside
 * would latch itself that way. */
static uint8_t p5 = 0xFF;
static uint8_t SCL_M = 0x10;            /* P5.4, assumption — will be checked */
static uint8_t SDA_M = 0x20;            /* P5.5 */

static void i2c_pause(void)
{
    volatile uint8_t w;

    for (w = 0; w < 10; w++)
        ;
}

static void i2c_init(void)
{
    p5 |= (uint8_t)(SCL_M | SDA_M);
    P5 = p5;
    P5M1 |= (uint8_t)(SCL_M | SDA_M);   /* open collector, */
    P5M0 |= (uint8_t)(SCL_M | SDA_M);   /* the board has pull-ups */
}

static void scl(uint8_t hoch)
{
    if (hoch) p5 |= SCL_M; else p5 = (uint8_t)(p5 & ~SCL_M);
    P5 = p5;
    i2c_pause();
}

static void sda(uint8_t hoch)
{
    if (hoch) p5 |= SDA_M; else p5 = (uint8_t)(p5 & ~SDA_M);
    P5 = p5;
    i2c_pause();
}

static uint8_t sda_pegel(void)
{
    return (uint8_t)((P5 & SDA_M) ? 1 : 0);
}

static void i2c_start(void)
{
    sda(1); scl(1); sda(0); scl(0);
}

static void i2c_stop(void)
{
    sda(0); scl(1); sda(1);
}

/* Returns 1 if the device acknowledged. */
static uint8_t i2c_schreiben(uint8_t v)
{
    uint8_t i, quittung;

    for (i = 0; i < 8; i++) {
        scl(0);
        sda((uint8_t)(v & 0x80));
        scl(1);
        v = (uint8_t)(v << 1);
    }
    scl(0);
    sda(1);                             /* release the line for the device */
    scl(1);
    quittung = (uint8_t)(sda_pegel() == 0);
    scl(0);
    return quittung;
}

static uint8_t i2c_lesen(uint8_t bestaetigen)
{
    uint8_t i, v = 0;

    sda(1);
    for (i = 0; i < 8; i++) {
        scl(0);
        scl(1);
        v = (uint8_t)((v << 1) | sda_pegel());
    }
    scl(0);
    sda((uint8_t)(bestaetigen ? 0 : 1));
    scl(1);
    scl(0);
    sda(1);
    return v;
}

static uint8_t antwortet(void)
{
    uint8_t da;

    i2c_init();
    i2c_start();
    da = i2c_schreiben(DS3231);
    i2c_stop();
    return da;
}

/* Seconds, minutes, hours — raw in BCD. */
static uint8_t roh[3];

static uint8_t zeit_lesen(void)
{
    uint8_t i;

    i2c_init();
    i2c_start();
    if (!i2c_schreiben(DS3231))     { i2c_stop(); return 0; }
    if (!i2c_schreiben(0x00))       { i2c_stop(); return 0; }
    i2c_start();                        /* repeated start */
    if (!i2c_schreiben(DS3231 | 1)) { i2c_stop(); return 0; }
    for (i = 0; i < 3; i++)
        roh[i] = i2c_lesen((uint8_t)(i < 2));
    i2c_stop();
    return 1;
}

static uint8_t zubcd(uint8_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}


static uint8_t sqw(uint8_t wert)
{
    i2c_init();
    i2c_start();
    if (!i2c_schreiben(DS3231)) { i2c_stop(); return 0; }
    if (!i2c_schreiben(0x0E))   { i2c_stop(); return 0; }
    if (!i2c_schreiben(wert))   { i2c_stop(); return 0; }
    i2c_stop();
    return 1;
}

/* ---------------------------------------------------------- Measurement */

/* All pins except the UART lines. The matrix pins are explicitly included:
 * no free pin reacted to touch, so the pads must hang via R1/R2 (1 MOhm) on
 * pins that also drive the display. When driving LEDs 1 MOhm is not
 * noticeable, in the RC measurement it is. */
#define ANZAHL 32
static const uint8_t PIN[ANZAHL] = {
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,               /* rows */
    0x32, 0x33, 0x34, 0x35, 0x36, 0x37,                     /* P3.2-P3.7 */
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,         /* P0 */
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15,                     /* P1.0-P1.5 */
    0x40, 0x41, 0x42,                                       /* P4.0-P4.2 */
    0x17,                                                   /* P1.7 */
    0x16        /* P1.6, the light sensor - as a positive control:
                 * an LDR changes its resistance by orders of magnitude.
                 * If the measurement shows no deflection here, it is
                 * worthless, and the silent touch pins then prove nothing. */
};
static __xdata uint16_t ruhe[ANZAHL];

static uint8_t latch[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void modus(uint8_t p, uint8_t b, uint8_t m)
{
    uint8_t ma = (uint8_t)(1u << b);
    uint8_t m1 = (m & 2) ? ma : 0;
    uint8_t m0 = (m & 1) ? ma : 0;

    switch (p) {
    case 0: P0M1 = (uint8_t)((P0M1 & ~ma) | m1); P0M0 = (uint8_t)((P0M0 & ~ma) | m0); break;
    case 1: P1M1 = (uint8_t)((P1M1 & ~ma) | m1); P1M0 = (uint8_t)((P1M0 & ~ma) | m0); break;
    case 2: P2M1 = (uint8_t)((P2M1 & ~ma) | m1); P2M0 = (uint8_t)((P2M0 & ~ma) | m0); break;
    case 3: P3M1 = (uint8_t)((P3M1 & ~ma) | m1); P3M0 = (uint8_t)((P3M0 & ~ma) | m0); break;
    default: P4M1 = (uint8_t)((P4M1 & ~ma) | m1); P4M0 = (uint8_t)((P4M0 & ~ma) | m0); break;
    }
}

static void setzen(uint8_t p, uint8_t b, uint8_t hoch)
{
    uint8_t ma = (uint8_t)(1u << b);

    if (hoch) latch[p] |= ma; else latch[p] = (uint8_t)(latch[p] & ~ma);
    switch (p) {
    case 0: P0 = latch[0]; break;
    case 1: P1 = latch[1]; break;
    case 2: P2 = latch[2]; break;
    case 3: P3 = latch[3] | 0x03; break;    /* never touch the UART */
    default: P4 = latch[4]; break;
    }
}

static uint8_t pegel(uint8_t p, uint8_t b)
{
    uint8_t ma = (uint8_t)(1u << b);

    switch (p) {
    case 0: return (uint8_t)((P0 & ma) ? 1 : 0);
    case 1: return (uint8_t)((P1 & ma) ? 1 : 0);
    case 2: return (uint8_t)((P2 & ma) ? 1 : 0);
    case 3: return (uint8_t)((P3 & ma) ? 1 : 0);
    default: return (uint8_t)((P4 & ma) ? 1 : 0);
    }
}

/* Charge sharing.
 *
 * The first approach - drive high, release, count until it drops - could
 * not see this circuit in principle: the pad hangs on the pin via 1 MOhm
 * and has no other DC path, so the pin always saturates. Worse still, the
 * long low drive discharged the pad as well.
 *
 * The right way: fully charge the pad over several time constants, then
 * discharge the pin node only very briefly and release it. The pad pushes
 * its charge back through the 1 MOhm; the two capacitances share it. The
 * larger the pad - and a finger enlarges it - the faster the pin reaches
 * the threshold again. Pins without a pad stay low and run into the upper
 * limit; that alone separates them.
 */
static uint16_t teilung(uint8_t p, uint8_t b)
{
    uint16_t n = 0;
    uint16_t w;

    modus(p, b, 1);
    setzen(p, b, 1);
    for (w = 0; w < 1200; w++)          /* about 1 ms: fully charge the pad */
        ;
    setzen(p, b, 0);                    /* briefly empty the pin node */
    setzen(p, b, 0);
    modus(p, b, 2);                     /* release and count */
    while (n < OBERGRENZE && !pegel(p, b))
        n++;
    return n;
}

static uint16_t mittel(uint8_t i, uint8_t p, uint8_t b)
{
    uint8_t k;
    uint16_t s = 0;

    (void)i;
    for (k = 0; k < 8; k++)
        s += teilung(p, b) >> 3;
    return s;
}

static void zahl(uint16_t v)
{
    uint8_t z[5], i = 0;

    if (!v) { uart_putc('0'); return; }
    while (v) { z[i++] = (uint8_t)('0' + v % 10); v /= 10; }
    while (i) uart_putc(z[--i]);
}

static void vorzeichen(int16_t v)
{
    if (v < 0) { uart_putc('-'); v = (int16_t)-v; }
    else uart_putc('+');
    zahl((uint16_t)v);
}

void main(void)
{
    uint8_t c, i, p, b;
    uint8_t stelle = 0, hoch;
    uint16_t jetzt;

    ports_all_highz();
    uart_init();
    sqw(0x04);                          /* buzzer off */
    uart_puts("\r\nSTC8 TOUCH v2 - Pin fuer Pin auf 0 V\r\n"
              "'m' faengt vorne an, 'n' geht einen Pin weiter, 'R' Bootloader\r\n"
              "Voltmeter zwischen R1 bzw. R2 und GND: Rund 3 V bedeutet,\r\n"
              "der angezeigte Pin ist der gesuchte.\r\n");

    for (;;) {
        if (!uart_avail())
            continue;
        c = uart_getc();

        if (c == 'R') {
            uart_puts("Neustart...\r\n");
            sqw(0x04);
            ports_all_highz();
            RESET_IN_BOOTLOADER();
        } else if (c == 'u') {
            /* P3.0 and P3.1 are the last untested pins. They carry the UART,
             * so they cannot be driven in dialogue: whoever pulls them low
             * cuts their own connection. So announce first, then lead time,
             * then drive, then restore the UART. */
            c = uart_getc();
            b = (uint8_t)(c == '1' ? 1 : 0);
            uart_puts("P3."); uart_putc((uint8_t)('0' + b));
            uart_puts(" kommt in 3 s fuer 8 s auf 3,3 V. "
                      "Pruefer bereithalten.\r\n");
            for (i = 0; i < 3; i++)
                for (jetzt = 0; jetzt < 2500; jetzt++) {
                    volatile uint16_t w;
                    for (w = 0; w < 400; w++)
                        ;
                }
            modus(3, b, 1);
            setzen(3, b, 1);
            P3 = (uint8_t)(P3 | (1u << b));
            for (i = 0; i < 8; i++)
                for (jetzt = 0; jetzt < 2500; jetzt++) {
                    volatile uint16_t w;
                    for (w = 0; w < 400; w++)
                        ;
                }
            ports_all_highz();
            uart_init();
            uart_puts("vorbei\r\n");
        } else if (c == 'm' || c == 'n') {
            /* The pin is driven to 3.3 V, not to 0 V, and stays there until
             * the next 'n'.
             *
             * Pulling it low first and searching with the continuity tester
             * was useless twice: from the pad end there is 1 MOhm in
             * between, no tester beeps there; and from the pin end the
             * output transistor only pulls to 30-50 Ohm, which is just above
             * the beep threshold of many meters.
             *
             * With high and a voltmeter both problems disappear: a meter has
             * 10 MOhm input resistance, through 1 MOhm about 3 V of it still
             * remain - clearly measurable, from both ends. */
            if (c == 'm')
                stelle = 0;
            else if (stelle < ANZAHL)
                stelle++;

            /* release the previous pin */
            if (stelle > 0 && stelle <= ANZAHL) {
                p = (uint8_t)(PIN[stelle - 1] >> 4);
                b = (uint8_t)(PIN[stelle - 1] & 0x0F);
                modus(p, b, 2);
            }
            if (stelle >= ANZAHL) {
                uart_puts("durch - 'm' faengt von vorn an\r\n");
            } else {
                p = (uint8_t)(PIN[stelle] >> 4);
                b = (uint8_t)(PIN[stelle] & 0x0F);
                /* Drive rows (port 2) only low, never high:
                 * a row at 3.3 V sends current through all 25 LEDs of that
                 * row into the column pins - including P3.0, the receive
                 * line. It then hangs at an undefined middle potential, and
                 * the chip no longer accepts commands. That is exactly how
                 * the firmware went silent twice.
                 *
                 * At low the LEDs are reverse-biased, nothing flows.
                 * Columns, on the other hand, may go high, because the LEDs
                 * are reverse-biased there too. */
                hoch = (uint8_t)(p != 2);
                modus(p, b, 1);
                setzen(p, b, hoch);
                uart_puts("P"); uart_putc((uint8_t)('0' + p));
                uart_putc('.'); uart_putc((uint8_t)('0' + b));
                uart_puts(hoch ? " liegt auf 3,3 V   (" : " liegt auf 0 V   (");
                zahl((uint16_t)(stelle + 1));
                uart_puts(" von ");
                zahl(ANZAHL);
                uart_puts(")\r\n");
            }
        }
    }
}
