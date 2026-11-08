/*
 * Minimal test: proves compiler, flashing, clock and UART in one go.
 *
 * After start-up the chip reports with a banner and then echoes every
 * received character. 'v' repeats the banner.
 *
 * The display stays dark and the buzzer silent — all pins are
 * high-impedance. That is intended: this test deliberately does not touch
 * the hardware.
 */
#include "stc8.h"

static void banner(void)
{
    uart_puts("\r\nSTC8 MATRIXCLOCK HELLO v1\r\n");
    uart_puts("fosc=18432000 baud=115200 ports=highz\r\n");
    uart_puts("echo aktiv, 'v' wiederholt dieses Banner\r\n");
}

void main(void)
{
    uint8_t c;

    ports_all_highz();
    uart_init();
    banner();

    for (;;) {
        c = uart_getc();
        if (c == 'v') {
            banner();
            continue;
        }
        /* echo with hex display, so that non-printable characters are visible too */
        uart_puts("rx 0x");
        uart_puthex8(c);
        if (c >= 0x20 && c < 0x7F) {
            uart_puts(" '");
            uart_putc(c);
            uart_puts("'");
        }
        uart_puts("\r\n");
    }
}
