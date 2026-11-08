/*
 * Shows a pattern on the display - the acid test for the pinout that was
 * found.
 *
 * Structure according to measurement: 25 columns x 7 rows, 175 LEDs.
 *   rows    (source, high): P2.0 ... P2.6
 *   columns (sink,   low ): P3.0-P3.7, P0.0-P0.7, P1.0-P1.5, P4.0-P4.2
 *
 * Scanning is column-wise: one column low, the desired rows high, wait
 * briefly, next. Each LED is therefore lit 1/25 of the time. The other way
 * round - one row sourcing, 25 columns sinking - the current through the
 * one row pin would be three times as high.
 *
 * While displaying, the UART is dead: P3.0 and P3.1 are columns themselves.
 * So each pattern runs only for a limited time and then reports back.
 *
 * Commands:  m0 all on        m1 frame        m2 "12:34"
 *            m3 arrow at column 1             o off     R bootloader
 */
#include "stc8.h"

#define SPALTEN  25
#define ZEILEN    7

/* column pins in display order, as (port << 4) | bit. */
static const uint8_t SPALTE[SPALTEN] = {
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,   /* P3.0 … P3.7 */
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,   /* P0.0 … P0.7 */
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15,               /* P1.0 … P1.5 */
    0x40, 0x41, 0x42                                  /* P4.0 … P4.2 */
};

/* 5x7 font, column-wise, bit 0 is the top row (P2.0). */
static const uint8_t ZIFFER[10][5] = {
    { 0x3E, 0x51, 0x49, 0x45, 0x3E },   /* 0 */
    { 0x00, 0x42, 0x7F, 0x40, 0x00 },   /* 1 */
    { 0x42, 0x61, 0x51, 0x49, 0x46 },   /* 2 */
    { 0x21, 0x41, 0x45, 0x4B, 0x31 },   /* 3 */
    { 0x18, 0x14, 0x12, 0x7F, 0x10 },   /* 4 */
    { 0x27, 0x45, 0x45, 0x45, 0x39 },   /* 5 */
    { 0x3C, 0x4A, 0x49, 0x49, 0x30 },   /* 6 */
    { 0x01, 0x71, 0x09, 0x05, 0x03 },   /* 7 */
    { 0x36, 0x49, 0x49, 0x49, 0x36 },   /* 8 */
    { 0x06, 0x49, 0x49, 0x29, 0x1E }    /* 9 */
};

static __xdata uint8_t bild[SPALTEN];

static void port_write(uint8_t p, uint8_t v)
{
    switch (p) {
    case 0: P0 = v; break;
    case 1: P1 = v; break;
    case 3: P3 = v; break;
    default: P4 = v; break;
    }
}

static uint8_t latch[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void spalte_an(uint8_t p, uint8_t b)
{
    uint8_t m = (uint8_t)(1u << b);

    latch[p] = (uint8_t)(latch[p] & ~m);
    port_write(p, latch[p]);
    switch (p) {                        /* push-pull, pulls low */
    case 0: P0M1 &= (uint8_t)~m; P0M0 |= m; break;
    case 1: P1M1 &= (uint8_t)~m; P1M0 |= m; break;
    case 3: P3M1 &= (uint8_t)~m; P3M0 |= m; break;
    default: P4M1 &= (uint8_t)~m; P4M0 |= m; break;
    }
}

static void spalte_aus(uint8_t p, uint8_t b)
{
    uint8_t m = (uint8_t)(1u << b);

    switch (p) {                        /* high-impedance again */
    case 0: P0M1 |= m; P0M0 &= (uint8_t)~m; break;
    case 1: P1M1 |= m; P1M0 &= (uint8_t)~m; break;
    case 3: P3M1 |= m; P3M0 &= (uint8_t)~m; break;
    default: P4M1 |= m; P4M0 &= (uint8_t)~m; break;
    }
    latch[p] |= m;
    port_write(p, latch[p]);
}

static void zeigen(uint16_t rahmen)
{
    uint16_t r;
    uint8_t c, pb, p, b;
    volatile uint8_t w;

    /* P2.0…P2.6 push-pull, P2.7 stays high-impedance (those are the rear LEDs). */
    P2M1 &= 0x80;
    P2M0 |= 0x7F;

    for (r = 0; r < rahmen; r++) {
        for (c = 0; c < SPALTEN; c++) {
            pb = SPALTE[c];
            p = (uint8_t)(pb >> 4);
            b = (uint8_t)(pb & 0x0F);

            P2 = (uint8_t)((P2 & 0x80) | (bild[c] & 0x7F));
            spalte_an(p, b);
            for (w = 0; w < 50; w++)
                ;
            spalte_aus(p, b);
            P2 = (uint8_t)(P2 & 0x80);
        }
    }
    ports_all_highz();
}

static void leeren(void)
{
    uint8_t i;

    for (i = 0; i < SPALTEN; i++)
        bild[i] = 0;
}

static void ziffer_setzen(uint8_t ab, uint8_t z)
{
    uint8_t i;

    for (i = 0; i < 5; i++)
        bild[ab + i] = ZIFFER[z][i];
}

static void muster_waehlen(uint8_t n)
{
    uint8_t i;

    leeren();
    switch (n) {
    case 0:                             /* all on */
        for (i = 0; i < SPALTEN; i++)
            bild[i] = 0x7F;
        break;
    case 1:                             /* frame */
        for (i = 0; i < SPALTEN; i++)
            bild[i] = 0x41;
        bild[0] = 0x7F;
        bild[SPALTEN - 1] = 0x7F;
        break;
    case 2:                             /* 12:34 */
        ziffer_setzen(1, 1);
        ziffer_setzen(7, 2);
        bild[12] = 0x14;                /* colon */
        ziffer_setzen(13, 3);
        ziffer_setzen(19, 4);
        break;
    default:                            /* arrow at column 1 */
        bild[0] = 0x08;
        bild[1] = 0x1C;
        bild[2] = 0x3E;
        bild[3] = 0x7F;
        for (i = 4; i < 12; i++)
            bild[i] = 0x08;
        break;
    }
}

void main(void)
{
    uint8_t c, n;

    ports_all_highz();
    uart_init();
    uart_puts("\r\nSTC8 MUSTER v1 - 25 Spalten x 7 Zeilen\r\n");
    uart_puts("'m0' alles an, 'm1' Rahmen, 'm2' 12:34, 'm3' Pfeil\r\n");
    uart_puts("'o' aus, 'R' Bootloader. Waehrend der Anzeige schweigt die UART.\r\n");
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
        } else if (c == 'm') {
            n = uart_getc();
            if (n < '0' || n > '3') {
                uart_puts("ungueltig\r\n");
            } else {
                muster_waehlen((uint8_t)(n - '0'));
                uart_puts("zeige...\r\n");
                zeigen(6000);
                uart_init();
                uart_puts("fertig\r\n");
            }
        }
    }
}
