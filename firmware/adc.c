/*
 * Read all 15 analog inputs continuously.
 *
 * Why: up to here I had only measured with digital thresholds - drive a
 * pin, see whether another node flips. That failed the positive control:
 * even at P1.6, where the light sensor demonstrably hangs, the voltage
 * moved by only 40 mV. A method that does not see a known connection
 * cannot say anything about unknown ones.
 *
 * The ADC resolves 12 bits and therefore sees what a threshold measurement
 * swallows. Channels: ADC0-7 = P1.0-P1.7, ADC8-14 = P0.0-P0.6.
 *
 * Commands:  l  measure continuously (any character ends)
 *            R  bootloader
 */
#include "stc8.h"

__sfr __at (0xBC) ADC_CONTR;
__sfr __at (0xBD) ADC_RES;
__sfr __at (0xBE) ADC_RESL;
__sfr __at (0xDE) ADCCFG;

#define KANAELE 15
#define DS3231  0xD0


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


/* The buzzer hangs off the DS3231 square-wave output, not the controller.
 * Register 0x0E: bit 2 off = SQW active, bits 4/3 select the frequency. */
static uint8_t buzzer(uint8_t wert)
{
    i2c_init();
    i2c_start();
    if (!i2c_schreiben(DS3231)) { i2c_stop(); return 0; }
    if (!i2c_schreiben(0x0E))   { i2c_stop(); return 0; }
    if (!i2c_schreiben(wert))   { i2c_stop(); return 0; }
    i2c_stop();
    return 1;
}

static void adc_init(void)
{
    volatile uint16_t w;

    ADCCFG = 0x2F;                  /* right-aligned, slowest conversion */
    ADC_CONTR = 0x80;               /* switch on */
    for (w = 0; w < 2000; w++)      /* settling */
        ;
}

static uint16_t adc_lesen(uint8_t kanal)
{
    volatile uint8_t w;

    ADC_CONTR = (uint8_t)(0x80 | (kanal & 0x0F));
    for (w = 0; w < 20; w++)        /* let the input settle */
        ;
    ADC_CONTR |= 0x40;              /* START */
    while (!(ADC_CONTR & 0x20))
        ;
    ADC_CONTR = (uint8_t)(ADC_CONTR & ~0x20);
    return (uint16_t)(((uint16_t)ADC_RES << 8) | ADC_RESL);
}

/* Measure several times and average - single conversions are noisy by a
 * few counts, and that is exactly the order of magnitude we expect from a
 * finger. */
static uint16_t adc_mittel(uint8_t kanal)
{
    uint8_t i;
    uint16_t s = 0;

    for (i = 0; i < 8; i++)
        s += adc_lesen(kanal);
    return (uint16_t)(s >> 3);
}

/* Peak-to-valley over 32 fast conversions.
 *
 * The finger cannot be recognised by the mean - a floating pin drifts
 * anyway. But it can by the restlessness: a hand couples in mains hum, and
 * 50 Hz turn a quiet channel into a jittery one. That is exactly what this
 * function measures. Static measurements were blind to it. */
static uint16_t unruhe(uint8_t kanal)
{
    uint8_t i;
    uint16_t v, klein = 4095, gross = 0;

    for (i = 0; i < 32; i++) {
        v = adc_lesen(kanal);
        if (v < klein) klein = v;
        if (v > gross) gross = v;
    }
    return (uint16_t)(gross - klein);
}

/* Charge sharing with the ADC's sampling capacitor.
 *
 * Charge the pin to 3.3 V, release it, then convert eight times in a row.
 * Each conversion draws charge into the internal sampling capacitor, so the
 * voltage drops in steps. How fast depends on the ratio of the two
 * capacitances - and a large copper area with a finger on it is many times
 * larger than a bare pin.
 *
 * This is the first measurement that can see a bare pad without a DC path
 * at all. Discharge time, charge sharing via 1 MOhm and restlessness
 * measurement were all blind to it in principle. */
static uint16_t absacken(uint8_t kanal)
{
    uint8_t i, p, b, m;
    volatile uint8_t w;

    p = (uint8_t)(kanal < 8 ? 1 : 0);
    b = (uint8_t)(kanal < 8 ? kanal : kanal - 8);
    m = (uint8_t)(1u << b);

    if (p == 1) { P1M1 &= (uint8_t)~m; P1M0 |= m; P1 |= m; }
    else        { P0M1 &= (uint8_t)~m; P0M0 |= m; P0 |= m; }
    for (w = 0; w < 80; w++)            /* charge */
        ;
    if (p == 1) { P1M1 |= m; P1M0 &= (uint8_t)~m; }   /* release */
    else        { P0M1 |= m; P0M0 &= (uint8_t)~m; }

    for (i = 0; i < 7; i++)
        adc_lesen(kanal);
    return adc_lesen(kanal);            /* eighth conversion */
}

static void zahl4(uint16_t v)
{
    uart_putc((uint8_t)('0' + (v / 1000) % 10));
    uart_putc((uint8_t)('0' + (v / 100) % 10));
    uart_putc((uint8_t)('0' + (v / 10) % 10));
    uart_putc((uint8_t)('0' + v % 10));
}

void main(void)
{
    uint8_t c, k;

    ports_all_highz();              /* analog inputs must be high-impedance */
    uart_init();
    adc_init();
    buzzer(0x04);
    uart_puts("\r\nSTC8 ADC v1 - 15 Kanaele\r\n"
              "Reihenfolge: P1.0 P1.1 P1.2 P1.3 P1.4 P1.5 P1.6 P1.7 "
              "P0.0 P0.1 P0.2 P0.3 P0.4 P0.5 P0.6\r\n"
              "'l' Mittelwerte, 'v' Unruhe, 'k' Kapazitaet\r\n"
              "'b0'-'b3' Buzzer aus / 1k / 4k / 8k, 'R' Bootloader\r\n");

    for (;;) {
        if (!uart_avail())
            continue;
        c = uart_getc();

        if (c == 'R') {
            uart_puts("Neustart...\r\n");
            ports_all_highz();
            RESET_IN_BOOTLOADER();
        } else if (c == 'b') {
            /* sound source on the board itself - the PC has no
             * speakers, all analog outputs report "not connected". */
            k = uart_getc();
            if (k == '0')      buzzer(0x04);
            else if (k == '1') buzzer(0x08);
            else if (k == '2') buzzer(0x10);
            else               buzzer(0x18);
            uart_puts("ok\r\n");
        } else if (c == 'k') {
            while (!uart_avail()) {
                for (k = 0; k < KANAELE; k++) {
                    zahl4(absacken(k));
                    uart_putc(' ');
                }
                uart_puts("\r\n");
            }
            uart_getc();
            ports_all_highz();
            uart_puts("Ende\r\n");
        } else if (c == 'v') {
            while (!uart_avail()) {
                for (k = 0; k < KANAELE; k++) {
                    zahl4(unruhe(k));
                    uart_putc(' ');
                }
                uart_puts("\r\n");
            }
            uart_getc();
            uart_puts("Ende\r\n");
        } else if (c == 'l') {
            while (!uart_avail()) {
                for (k = 0; k < KANAELE; k++) {
                    zahl4(adc_mittel(k));
                    uart_putc(' ');
                }
                uart_puts("\r\n");
            }
            uart_getc();
            uart_puts("Ende\r\n");
        }
    }
}
