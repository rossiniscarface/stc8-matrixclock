/*
 * Find the buzzer. Only three pins remain: P1.7, P4.3, P4.4 — all others
 * are taken by the matrix, I2C, light sensor or rear LEDs.
 *
 * Two lessons from the failed first attempt:
 *  - Back then it was clocked at a fixed 2 kHz. A piezo is hardly audible
 *    outside its resonance, so here 1.2 to 6 kHz are swept.
 *  - Back then one had to guess which pin was being driven. Now the NUMBER
 *    of beeps encodes the pin: once P1.7, twice P4.3, three times P4.4.
 *
 * Commands:  a  all three in turn      bPB  only this pin
 *            R  bootloader
 */
#include "stc8.h"

static void pin_mode(uint8_t p, uint8_t bit, uint8_t modus)
{
    uint8_t m = (uint8_t)(1u << bit);
    uint8_t m1 = (modus & 2) ? m : 0;
    uint8_t m0 = (modus & 1) ? m : 0;

    switch (p) {
    case 1: P1M1 = (uint8_t)((P1M1 & ~m) | m1); P1M0 = (uint8_t)((P1M0 & ~m) | m0); break;
    default: P4M1 = (uint8_t)((P4M1 & ~m) | m1); P4M0 = (uint8_t)((P4M0 & ~m) | m0); break;
    }
}

static uint8_t latch1 = 0xFF, latch4 = 0xFF;

static void pin_set(uint8_t p, uint8_t bit, uint8_t hoch)
{
    uint8_t m = (uint8_t)(1u << bit);

    if (p == 1) {
        if (hoch) latch1 |= m; else latch1 = (uint8_t)(latch1 & ~m);
        P1 = latch1;
    } else {
        if (hoch) latch4 |= m; else latch4 = (uint8_t)(latch4 & ~m);
        P4 = latch4;
    }
}

/* One loop round takes about 0.6 us; halb=400 gives a measured 2 kHz. */
static void ton(uint8_t p, uint8_t b, uint16_t halb, uint16_t perioden)
{
    uint16_t z;
    volatile uint16_t w;

    for (z = 0; z < perioden; z++) {
        pin_set(p, b, 1);
        for (w = 0; w < halb; w++)
            ;
        pin_set(p, b, 0);
        for (w = 0; w < halb; w++)
            ;
    }
}

static void pause(uint16_t einheiten)
{
    uint16_t z;
    volatile uint16_t w;

    for (z = 0; z < einheiten; z++)
        for (w = 0; w < 400; w++)
            ;
}

static void wobbeln(uint8_t p, uint8_t b)
{
    uint16_t h;

    pin_mode(p, b, 1);                  /* push-pull */
    for (h = 400; h > 60; h -= 8)
        ton(p, b, h, 50);
    pin_mode(p, b, 2);                  /* high-impedance again */
    pin_set(p, b, 1);
}

static void piepsen(uint8_t p, uint8_t b, uint8_t mal)
{
    uint8_t i;

    for (i = 0; i < mal; i++) {
        wobbeln(p, b);
        pause(600);                     /* about 0.3 s between the tones */
    }
}

static uint8_t ziffer(uint8_t c)
{
    return (c >= '0' && c <= '9') ? (uint8_t)(c - '0') : 0xFF;
}

void main(void)
{
    uint8_t c, p, b;

    ports_all_highz();
    uart_init();
    uart_puts("\r\nSTC8 PIEP v1\r\n");
    uart_puts("'aPB' = Dauer-Low, Dauer-High, Wobbelton auf P<P>.<B>\r\n");
    uart_puts("'bPB' nur der Wobbelton, 'R' Bootloader\r\nbereit\r\n");

    for (;;) {
        if (!uart_avail())
            continue;
        c = uart_getc();

        if (c == 'R') {
            uart_puts("Neustart...\r\n");
            ports_all_highz();
            RESET_IN_BOOTLOADER();
        } else if (c == 'a') {
            /* One pin, three modes: constant low and constant high for an
             * active buzzer (it has its own oscillator and only needs
             * voltage), then the sweep tone for a passive one. The PC
             * announces which pin. */
            p = ziffer(uart_getc());
            b = ziffer(uart_getc());
            if ((p != 1 && p != 4) || b > 7) {
                uart_puts("nur Port 1 oder 4\r\n");
            } else {
                pin_mode(p, b, 1);
                pin_set(p, b, 0);       /* constant low */
                pause(2000);
                pin_set(p, b, 1);       /* constant high */
                pause(2000);
                pin_mode(p, b, 2);
                wobbeln(p, b);
                pin_mode(p, b, 2);
                pin_set(p, b, 1);
                uart_puts("fertig\r\n");
            }
        } else if (c == 'b') {
            p = ziffer(uart_getc());
            b = ziffer(uart_getc());
            if ((p != 1 && p != 4) || b > 7) {
                uart_puts("nur Port 1 oder 4\r\n");
            } else {
                uart_puts("Ton...\r\n");
                wobbeln(p, b);
                uart_puts("fertig\r\n");
            }
        }
    }
}
