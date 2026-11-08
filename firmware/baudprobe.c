/*
 * Baud rate probe.
 *
 * Problem: hello.c sent unreadable stuff and then went silent. It is
 * unclear whether the chip really runs at 18.432 MHz and whether the 1T bit
 * for timer 2 takes effect. Guessing does not help, so this firmware
 * measures itself.
 *
 * It sends the same identifier one after another with seven different
 * reload values. The text says for which baud rate each value was
 * calculated. Whoever listens at a fixed rate sees exactly one identifier
 * cleanly — and the ratio gives the clock error.
 *
 * Example: listening at 115200, "B=009600" is readable -> the chip is
 * faster than assumed by a factor of 12, so the 1T bit does not take effect.
 *
 * uart_putc has an emergency brake here so that nothing hangs anymore.
 */
#include "stc8.h"

static const uint32_t RATEN[] = {
    9600UL, 19200UL, 38400UL, 57600UL, 115200UL, 150000UL, 230400UL
};
#define N_RATEN (sizeof(RATEN) / sizeof(RATEN[0]))

/* Sends a character, but gives up after a while instead of blocking. */
static void tx(uint8_t c)
{
    uint16_t schutz = 50000;
    SBUF = c;
    while (!TI && --schutz)
        ;
    TI = 0;
}

static void tx_str(const char *s)
{
    while (*s)
        tx((uint8_t)*s++);
}

static void tx_dez6(uint32_t v)
{
    char puffer[7];
    int8_t i;
    for (i = 5; i >= 0; i--) {
        puffer[i] = (char)('0' + (uint8_t)(v % 10UL));
        v /= 10UL;
    }
    puffer[6] = 0;
    tx_str(puffer);
}

static void baud_setzen(uint32_t baud)
{
    uint32_t teiler = FOSC / (4UL * baud);
    uint16_t reload = (uint16_t)(65536UL - teiler);

    AUXR &= (uint8_t)~0x10;            /* stop timer 2 */
    AUXR |= 0x04;                      /* T2x12: 1T mode */
    T2L = (uint8_t)(reload & 0xFF);
    T2H = (uint8_t)(reload >> 8);
    AUXR |= 0x10;                      /* start timer 2 */
    AUXR |= 0x01;                      /* UART1 uses timer 2 */
    SCON = 0x50;
    TI = 0;
    RI = 0;
}

static void warte(uint16_t runden)
{
    uint16_t a;
    uint8_t b;
    for (a = 0; a < runden; a++)
        for (b = 0; b < 200; b++)
            ;
}

void main(void)
{
    uint8_t i, w;

    ports_all_highz();

    for (;;) {
        for (i = 0; i < N_RATEN; i++) {
            baud_setzen(RATEN[i]);
            /* Send several times so that a listener reliably catches the block.
             * The 'U' (0x55) in front is a clean alternating bit pattern and
             * is also suitable for measuring with the oscilloscope. */
            for (w = 0; w < 6; w++) {
                tx_str("UUUUUUUU B=");
                tx_dez6(RATEN[i]);
                tx_str(" STC8PROBE\r\n");
            }
            warte(300);
        }
    }
}
