/*
 * Matrix scanner.
 *
 * An LED needs both sides: a source at high, a sink at low. Driving single
 * pins is therefore useless - it has to be pairs. On command this firmware
 * drives exactly one pair, all other pins stay high-impedance. If an LED
 * lights up, the camera sees *which*, and so both the role of the two pins
 * and the grid position are known.
 *
 * Command: xABCD   A/B = port/bit of the source (high)
 *                  C/D = port/bit of the sink   (low)
 *          o       all off
 *          R       restart into the bootloader
 *
 * Safety measures, because the matrix has no series resistors:
 *  - The pair is pulsed at 2 kHz instead of driven statically, which halves
 *    the mean current. The camera integrates that away anyway.
 *  - After about 500 ms the firmware switches off by itself. A forgotten
 *    pair therefore cannot burn permanently.
 *  - UART and I2C pins are locked, as is P2.7 (the rear LEDs).
 */
#include "stc8.h"

#define HALB     400u      /* loop passes per half wave -> 2 kHz */
#define PERIODEN 2000u     /* 2000 periods -> about 1 s, measured */
/* Extended from 500 ms to 1 s so that at 8 frames per second several
 * shots reliably fall into the lit window. Only that allows the camera
 * noise (peaks up to 57 against a signal of 240) to be averaged away. */

static uint8_t latch[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/* Normally P3.0/P3.1 stay untouched, otherwise the connection breaks off
 * in the middle of the command. For the 'y' command the protection is
 * deliberately lifted - at the time of driving nobody is talking anymore. */
static uint8_t uart_geschuetzt = 1;

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
    if (p == 3 && uart_geschuetzt)
        latch[3] |= 0x03;          /* do not touch the UART */
    port_write(p, latch[p]);
}

/* Locked: only UART, I2C and what does not exist on the LQFP44.
 * P2.7 is free: it also drives the two rear LEDs, but is at the same time
 * the eighth matrix column and must therefore be measurable. */
static uint8_t gesperrt(uint8_t p, uint8_t b)
{
    if (p > 5 || b > 7)
        return 1;
    if (p == 3 && b < 2)
        return 1;                   /* UART */
    if (p == 5)
        return 1;                   /* P5.4/P5.5 I2C, rest not present */
    if (p == 4 && b > 4)
        return 1;                   /* reacts to nothing, probably not bonded */
    /* P4.2 was wrongly locked here as "not bonded out". The assumption came
     * from a pinout, not from a measurement - and it kept the pin out of all
     * 812 pairs of the scan. P4.2 is in fact the 25th column of the display. */
    return 0;
}

static void paar_treiben(uint8_t qp, uint8_t qb, uint8_t sp, uint8_t sb)
{
    uint16_t z;
    volatile uint16_t w;

    pin_set(sp, sb, 0);
    pin_mode(sp, sb, 1);            /* sink firmly low */
    pin_mode(qp, qb, 1);            /* source push-pull */

    for (z = 0; z < PERIODEN; z++) {
        pin_set(qp, qb, 1);
        for (w = 0; w < HALB; w++)
            ;
        pin_set(qp, qb, 0);
        for (w = 0; w < HALB; w++)
            ;
    }

    pin_mode(qp, qb, 2);            /* both high-impedance again */
    pin_mode(sp, sb, 2);
    pin_set(qp, qb, 1);
    pin_set(sp, sb, 1);
}

/* Rough wait loop. One HALB loop takes about 0.25 ms - exactly the time
 * base with which paar_treiben() reaches a measured 2 kHz. */
static void warten(uint16_t einheiten)
{
    uint16_t z;
    volatile uint16_t w;

    for (z = 0; z < einheiten; z++)
        for (w = 0; w < HALB; w++)
            ;
}

/* For the delayed command: lock nothing except the impossible.
 * P5.4/P5.5 (I2C) and the P4 pins that are probably not bonded out must
 * be testable too - otherwise it remains open whether there is a 25th
 * column. Pulled permanently low as a sink, the I2C bus is merely busy;
 * no write access to the clock occurs. */
static uint8_t gesperrt_roh(uint8_t p, uint8_t b)
{
    if (p > 5 || b > 7)
        return 1;
    return 0;
}

static uint8_t hexziffer(uint8_t c)
{
    if (c >= '0' && c <= '9')
        return (uint8_t)(c - '0');
    return 0xFF;
}

void main(void)
{
    uint8_t c, qp, qb, sp, sb;

    ports_all_highz();
    uart_init();
    uart_puts("\r\nSTC8 MATRIX v1 baud=57600\r\n");
    uart_puts("'xABCD' Paar treiben (A.B=High, C.D=Low), 'yABCD' dito nach 2 s\r\n");
    uart_puts("'o' aus, 'R' Bootloader\r\n");
    uart_puts("bereit\r\n");

    for (;;) {
        if (!uart_avail())
            continue;
        c = uart_getc();

        if (c == 'R') {
            uart_puts("Neustart...\r\n");
            ports_all_highz();
            RESET_IN_BOOTLOADER();
        } else if (c == 'o') {
            ports_all_highz();
            uart_puts("aus\r\n");
        } else if (c == 'x') {
            qp = hexziffer(uart_getc());
            qb = hexziffer(uart_getc());
            sp = hexziffer(uart_getc());
            sb = hexziffer(uart_getc());
            if (qp == 0xFF || qb == 0xFF || sp == 0xFF || sb == 0xFF) {
                uart_puts("ungueltig\r\n");
            } else if (gesperrt(qp, qb) || gesperrt(sp, sb)) {
                uart_puts("gesperrt\r\n");
            } else if (qp == sp && qb == sb) {
                uart_puts("gleich\r\n");
            } else {
                paar_treiben(qp, qb, sp, sb);
                uart_puts("ok\r\n");
            }
        } else if (c == 'y') {
            /* Like 'x', but only after two seconds - and P3.0/P3.1 may take
             * part. The acknowledgement comes first, because while driving
             * the transmit line itself may be part of the matrix. */
            qp = hexziffer(uart_getc());
            qb = hexziffer(uart_getc());
            sp = hexziffer(uart_getc());
            sb = hexziffer(uart_getc());
            if (qp == 0xFF || qb == 0xFF || sp == 0xFF || sb == 0xFF) {
                uart_puts("ungueltig\r\n");
            } else if (gesperrt_roh(qp, qb) || gesperrt_roh(sp, sb)) {
                uart_puts("gesperrt\r\n");
            } else if (qp == sp && qb == sb) {
                uart_puts("gleich\r\n");
            } else {
                uart_puts("spaeter\r\n");
                warten(8000);               /* about 2 s lead time */
                uart_geschuetzt = 0;
                paar_treiben(qp, qb, sp, sb);
                uart_geschuetzt = 1;
                ports_all_highz();
                uart_init();
                uart_puts("fertig\r\n");
            }
        } else {
            continue;
        }
    }
}
