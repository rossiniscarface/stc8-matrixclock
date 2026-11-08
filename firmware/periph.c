/*
 * Peripheral prober: microphone, light sensor, touch, buzzer, rear LEDs.
 *
 * Three methods, because each component gives itself away differently:
 *
 *  'a'  ADC sweep over channels 0..14 (P1.0-P1.7 = ADC0-7, P0.0-P0.6 = ADC8-14).
 *       Microphone and LDR are analog. Which channel reacts to clapping or
 *       to darkening stands out when comparing several sweeps.
 *
 *  't'  RC charge time of the non-analog candidates. Pull the pin low
 *       briefly, release, count until it reads high again. A 1 MOhm pull-up
 *       on a touch pad gives a measurable count, and a finger increases the
 *       capacitance noticeably. Two sweeps - once touched, once not - show
 *       the touch pins.
 *
 *  'dPB' drive one pin with a square wave of about 2 kHz for one second.
 *       The buzzer becomes audible, an LED visible. Example: d43 = P4.3.
 *
 * Caution with 'd': the matrix has no series resistors. Clocking an unknown
 * pin for one second is therefore not entirely harmless - use sparingly and
 * preferably stick to the pull-up pins.
 */
#include "stc8.h"

__sfr __at (0xBC) ADC_CONTR;
__sfr __at (0xBD) ADC_RES;
__sfr __at (0xBE) ADC_RESL;
__sfr __at (0xDE) ADCCFG;

/*
 * All pins bonded out on the LQFP44 except UART (P3.0/P3.1) and
 * I2C (P5.4/P5.5).
 *
 * The first version had only five candidates and did not find the touch
 * pads: with 1 MOhm they charge too slowly, so the pin census wrongly
 * considered them floating. A 1 MOhm pull-up needs about 20 us, i.e. a good
 * hundred loop passes - easily measurable and clearly different from the
 * 60000 of a pin without any pull-up.
 */
#define N_DIG 34
static __code const uint8_t DIG_P[N_DIG] = {
    0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,
    3,3,3,3,3,3,
    4,4,4,4
};
static __code const uint8_t DIG_B[N_DIG] = {
    0,1,2,3,4,5,6,7,
    0,1,2,3,4,5,6,7,
    0,1,2,3,4,5,6,7,
    2,3,4,5,6,7,
    0,1,3,4
};

static uint8_t latch[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void port_write(uint8_t p, uint8_t v)
{
    switch (p) {
    case 0: P0 = v; break;
    case 1: P1 = v; break;
    case 2: P2 = v; break;
    case 3: P3 = v; break;
    case 4: P4 = v; break;
    default: P5 = v; break;
    }
}

static uint8_t port_read(uint8_t p)
{
    switch (p) {
    case 0: return P0;
    case 1: return P1;
    case 2: return P2;
    case 3: return P3;
    case 4: return P4;
    default: return P5;
    }
}

static void pin_mode(uint8_t p, uint8_t bit, uint8_t modus)
{
    uint8_t m = (uint8_t)(1u << bit);
    uint8_t m1 = (modus & 2) ? m : 0;
    uint8_t m0 = (modus & 1) ? m : 0;

    switch (p) {
    case 0: P0M1 = (uint8_t)((P0M1 & ~m) | m1); P0M0 = (uint8_t)((P0M0 & ~m) | m0); break;
    case 1: P1M1 = (uint8_t)((P1M1 & ~m) | m1); P1M0 = (uint8_t)((P1M0 & ~m) | m0); break;
    case 2: P2M1 = (uint8_t)((P2M1 & ~m) | m1); P2M0 = (uint8_t)((P2M0 & ~m) | m0); break;
    case 3: P3M1 = (uint8_t)((P3M1 & ~m) | m1); P3M0 = (uint8_t)((P3M0 & ~m) | m0); break;
    case 4: P4M1 = (uint8_t)((P4M1 & ~m) | m1); P4M0 = (uint8_t)((P4M0 & ~m) | m0); break;
    default: P5M1 = (uint8_t)((P5M1 & ~m) | m1); P5M0 = (uint8_t)((P5M0 & ~m) | m0); break;
    }
}

static void pin_set(uint8_t p, uint8_t bit, uint8_t hoch)
{
    uint8_t m = (uint8_t)(1u << bit);
    if (hoch)
        latch[p] |= m;
    else
        latch[p] = (uint8_t)(latch[p] & ~m);
    if (p == 3)
        latch[3] |= 0x03;
    port_write(p, latch[p]);
}

static void tx_dez(uint16_t v)
{
    char puf[6];
    int8_t i;
    for (i = 4; i >= 0; i--) {
        puf[i] = (char)('0' + (uint8_t)(v % 10u));
        v /= 10u;
    }
    puf[5] = 0;
    uart_puts(puf);
}

/* --- ADC --- */

static void adc_init(void)
{
    ADCCFG = 0x2F;              /* right-aligned, slowest conversion */
    ADC_CONTR = 0x80;           /* switch the ADC on */
}

static uint16_t adc_lesen(uint8_t kanal)
{
    volatile uint8_t w;

    ADC_CONTR = (uint8_t)(0x80 | (kanal & 0x0F));
    for (w = 0; w < 60; w++)        /* settling of the multiplexer */
        ;
    ADC_CONTR |= 0x40;              /* START */
    while (!(ADC_CONTR & 0x20))
        ;
    ADC_CONTR = (uint8_t)(ADC_CONTR & ~0x20);
    return (uint16_t)(((uint16_t)ADC_RES << 8) | ADC_RESL);
}

static void adc_durchlauf(void)
{
    uint8_t k;
    uart_puts("ADC");
    for (k = 0; k <= 14; k++) {
        uart_putc(' ');
        tx_dez(adc_lesen(k));
    }
    uart_puts("\r\n");
}

/* --- RC charge time for touch --- */

/* Discharge, release, count until high: finds pull-ups to Vcc. */
static uint16_t ladezeit(uint8_t p, uint8_t bit)
{
    uint16_t n = 0;
    volatile uint8_t w;

    pin_set(p, bit, 0);
    pin_mode(p, bit, 1);
    for (w = 0; w < 40; w++)
        ;
    pin_mode(p, bit, 2);
    while (n < 1500u && !((port_read(p) >> bit) & 1u))
        n++;
    return n;
}

/*
 * The opposite direction: charge, release, count until low.
 *
 * Needed because the 1 MOhm resistors R1/R2 are apparently not pull-ups to
 * Vcc - otherwise ladezeit() would have found them. If they are instead in
 * series with the touch pad or to ground, the pin drops after release, and
 * the slower the more capacitance is attached. A finger is exactly that.
 */
static uint16_t entladezeit(uint8_t p, uint8_t bit)
{
    uint16_t n = 0;
    volatile uint8_t w;

    pin_set(p, bit, 1);
    pin_mode(p, bit, 1);
    for (w = 0; w < 40; w++)
        ;
    pin_mode(p, bit, 2);
    while (n < 1500u && ((port_read(p) >> bit) & 1u))
        n++;
    return n;
}

/* Into external RAM: 2 x 34 x 2 bytes do not fit into the 128-byte DSEG. */
static __xdata uint16_t rc_auf[N_DIG];
static __xdata uint16_t rc_ab[N_DIG];

static void touch_durchlauf(void)
{
    uint8_t i;

    /* Measure everything first, then send. Sending while driving already
     * backfired once, when the supply dip chopped up the UART. */
    for (i = 0; i < N_DIG; i++) {
        rc_auf[i] = ladezeit(DIG_P[i], DIG_B[i]);
        rc_ab[i]  = entladezeit(DIG_P[i], DIG_B[i]);
    }

    uart_puts("RC");
    for (i = 0; i < N_DIG; i++) {
        uart_puts("  P");
        uart_putc((uint8_t)('0' + DIG_P[i]));
        uart_putc('.');
        uart_putc((uint8_t)('0' + DIG_B[i]));
        uart_putc('=');
        tx_dez(rc_auf[i]);
        uart_putc('/');
        tx_dez(rc_ab[i]);
    }
    uart_puts("\r\n");
}

/* --- clock one pin --- */

/*
 * The first version was useless: 40 passes of a uint8_t loop are about
 * 13 us, which makes 38 kHz instead of the claimed 2 kHz - beyond hearing.
 * And 2000 periods of it took 52 ms instead of one second. Neither audible
 * nor visible.
 *
 * Now a uint16_t loop with considerably more passes. The exact frequency is
 * unknown because the number of clocks per pass is not exactly known - so
 * the firmware reports start and end, so that the host can measure the
 * actual duration and calculate back from it.
 */
#define TAKT_HALB   400u        /* loop passes per half wave */
#define TAKT_ZAHL   2000u       /* number of periods */

static void takten(uint8_t p, uint8_t bit)
{
    uint16_t z;
    volatile uint16_t w;

    uart_puts("takte P");
    uart_putc((uint8_t)('0' + p));
    uart_putc('.');
    uart_putc((uint8_t)('0' + bit));
    uart_puts("\r\n");

    pin_mode(p, bit, 1);
    for (z = 0; z < TAKT_ZAHL; z++) {
        pin_set(p, bit, 0);
        for (w = 0; w < TAKT_HALB; w++)
            ;
        pin_set(p, bit, 1);
        for (w = 0; w < TAKT_HALB; w++)
            ;
    }
    pin_set(p, bit, 1);
    pin_mode(p, bit, 2);
    uart_puts("fertig\r\n");
}

void main(void)
{
    uint8_t c, p, b;

    ports_all_highz();
    uart_init();
    adc_init();

    uart_puts("\r\nSTC8 PERIPH v1 baud=57600\r\n");
    uart_puts("'a' ADC-Durchlauf, 't' RC-Ladezeiten, 'dPB' Pin takten, 'R' Bootloader\r\n");
    uart_puts("bereit\r\n");

    for (;;) {
        if (!uart_avail())
            continue;
        c = uart_getc();

        if (c == 'R') {
            uart_puts("Neustart...\r\n");
            ports_all_highz();
            RESET_IN_BOOTLOADER();
        } else if (c == 'a') {
            adc_durchlauf();
        } else if (c == 't') {
            touch_durchlauf();
        } else if (c == 'd') {
            p = (uint8_t)(uart_getc() - '0');
            b = (uint8_t)(uart_getc() - '0');
            if (p > 5 || b > 7 || (p == 3 && b < 2) || (p == 5 && (b == 4 || b == 5)))
                uart_puts("gesperrt (UART oder I2C)\r\n");
            else
                takten(p, b);
        } else {
            continue;
        }
        uart_puts("bereit\r\n");
    }
}
