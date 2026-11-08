/*
 * Fine measurement of the charge time at P4.3 and P4.4 - the last
 * candidates for the two touch pads.
 *
 * By elimination only these two remain: 32 pins drive the matrix, P1.6 is
 * the light sensor, P1.7 is at ground, P2.7 switches the rear LEDs,
 * P5.4/P5.5 are I2C, P4.5-P4.7 are not bonded out. The buzzer hangs off the
 * DS3231. P4.3 and P4.4 are populated, have a pull-up and demonstrably do
 * nothing else.
 *
 * Why nothing was visible so far: the pull-up is low-resistance, the charge
 * time is a fraction of a microsecond. A counting loop is too coarse and
 * always returns 0.
 *
 * Remedy: not measure finer, but more often. 256 repetitions summed up turn
 * a difference of a single loop pass into a three-digit difference in the
 * sum.
 *
 * Commands:  f  measure continuously (any character ends)   R  bootloader
 */
#include "stc8.h"

#define WIEDERHOLUNGEN 128u

static const uint8_t PIN[4] = { 0x43, 0x44, 0x40, 0x41 };   /* 4.0/4.1 for comparison */

static uint16_t ladesumme(uint8_t b)
{
    uint16_t summe = 0, r;
    uint8_t n;
    uint8_t m = (uint8_t)(1u << b);
    volatile uint8_t w;

    for (r = 0; r < WIEDERHOLUNGEN; r++) {
        P4M1 &= (uint8_t)~m;            /* push-pull */
        P4M0 |= m;
        P4 = (uint8_t)(P4 & ~m);        /* drive low, discharge pad */
        for (w = 0; w < 30; w++)
            ;
        P4M1 |= m;                      /* release, pull-up charges */
        P4M0 &= (uint8_t)~m;

        /* Window up to 64 passes: eight immediate reads (about 1.5 us) were too
         * short, the pin was then still low throughout. The averaging over
         * many repetitions provides the resolution, not the loop step. */
        n = 0;
        while (n < 64 && !(P4 & m))
            n++;
        summe += n;
    }
    return summe;
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
    uint8_t c, i;

    ports_all_highz();
    uart_init();
    uart_puts("\r\nSTC8 FEIN v1 - Ladezeit P4.3 P4.4 P4.0 P4.1\r\n"
              "'f' misst laufend, ein Zeichen beendet. 'R' Bootloader\r\n");

    for (;;) {
        if (!uart_avail())
            continue;
        c = uart_getc();

        if (c == 'R') {
            uart_puts("Neustart...\r\n");
            ports_all_highz();
            RESET_IN_BOOTLOADER();
        } else if (c == 'f') {
            while (!uart_avail()) {
                for (i = 0; i < 4; i++) {
                    zahl4(ladesumme((uint8_t)(PIN[i] & 0x0F)));
                    uart_putc(' ');
                }
                uart_puts("\r\n");
            }
            uart_getc();
            ports_all_highz();
            uart_puts("Ende\r\n");
        }
    }
}
