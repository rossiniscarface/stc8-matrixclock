/*
 * Pin census, two-stage.
 *
 * Pass A (runs immediately, drives nothing):
 *   All pins high-impedance, 64 measurements per pin, counting how often high.
 *   0x40 = always high, 0x00 = always low, anything in between = unstable.
 *
 * Pass B (only on command 'b'):
 *   Briefly pull each pin low, release, measure again — and the same with
 *   high. If the pin recovers quickly, there is an external pull-up or
 *   pull-down. If it stays, it is only floating. That separates real
 *   circuitry from chance, which pass A alone cannot do: an open CMOS input
 *   holds its charge and therefore reads stable even without a pull-up.
 *
 * Pass B drives pins, which is why it is deliberately not automatic.
 * P3.0/P3.1 are always left out, otherwise the UART dies.
 */
#include "stc8.h"

#define N_PORTS   6
#define SAMPLES   0x40

static uint8_t zaehler[8];

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

/* modus: 0 quasi-bidirectional, 1 push-pull, 2 high-impedance, 3 open-drain */
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

static uint8_t ist_uart_pin(uint8_t p, uint8_t bit)
{
    return (uint8_t)(p == 3 && bit < 2);
}

/*
 * Shadow latches instead of reading back.
 *
 * A port_write(p, port_read(p) | maske) would be a read-modify-write over
 * the whole port: it reads the pin levels and writes them back into all
 * eight latches. On port 3 a 0 ends up in P3.0/P3.1 sooner or later — and
 * the UART is dead. So the latch state is tracked here and the two UART
 * bits are forced on every write.
 */
static uint8_t latch[N_PORTS] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void pin_drive(uint8_t p, uint8_t bit, uint8_t hoch)
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

static void kurz(uint8_t n)
{
    volatile uint8_t i;
    for (i = 0; i < n; i++)
        ;
}

static void durchgang_a(void)
{
    uint8_t p, s, bit, v;

    uart_puts("PASS A hochohmig, ");
    uart_puthex8(SAMPLES);
    uart_puts(" Messungen je Pin\r\n");

    for (p = 0; p < N_PORTS; p++) {
        for (bit = 0; bit < 8; bit++)
            zaehler[bit] = 0;

        for (s = 0; s < SAMPLES; s++) {
            v = port_read(p);
            for (bit = 0; bit < 8; bit++)
                if (v & (1u << bit))
                    zaehler[bit]++;
            kurz(30);
        }

        uart_puts("A P");
        uart_putc((uint8_t)('0' + p));
        for (bit = 0; bit < 8; bit++) {
            uart_putc(' ');
            uart_puthex8(zaehler[bit]);
        }
        uart_puts("\r\n");
    }
}

/*
 * Results are collected first and sent afterwards.
 *
 * The first attempt printed while measuring — and arrived garbled: single
 * bits dropped, 'B' became '@'. The reason is the matrix without series
 * resistors. When such a pin is driven, the supply dips briefly and the
 * UART stumbles in the middle of a character. Separating measuring and
 * sending makes the problem go away.
 */
static uint8_t ergebnis[N_PORTS][8];

static void durchgang_b(void)
{
    uint8_t p, bit, nach_low, nach_high;

    for (p = 0; p < N_PORTS; p++)
        for (bit = 0; bit < 8; bit++)
            ergebnis[p][bit] = 0xFF;      /* 0xFF = not measured */

    for (p = 0; p < N_PORTS; p++) {
        for (bit = 0; bit < 8; bit++) {
            if (ist_uart_pin(p, bit))
                continue;

            /* briefly pull low, then release and look */
            pin_drive(p, bit, 0);
            pin_mode(p, bit, 1);
            kurz(20);
            pin_mode(p, bit, 2);
            kurz(10);
            nach_low = (uint8_t)((port_read(p) >> bit) & 1u);

            /* the same the other way round */
            pin_drive(p, bit, 1);
            pin_mode(p, bit, 1);
            kurz(20);
            pin_mode(p, bit, 2);
            kurz(10);
            nach_high = (uint8_t)((port_read(p) >> bit) & 1u);

            pin_mode(p, bit, 2);
            ergebnis[p][bit] = (uint8_t)((nach_low << 1) | nach_high);

            /* give the supply time to recover before the next pin */
            kurz(200);
        }
    }

    /* Only send now — no more driving that could disturb the UART. */
    uart_puts("PASS B Pull-Test, Format: Port Bit erholung_nach_low erholung_nach_high\r\n");
    for (p = 0; p < N_PORTS; p++) {
        for (bit = 0; bit < 8; bit++) {
            if (ergebnis[p][bit] == 0xFF)
                continue;
            uart_puts("B P");
            uart_putc((uint8_t)('0' + p));
            uart_putc('.');
            uart_putc((uint8_t)('0' + bit));
            uart_putc(' ');
            uart_putc((uint8_t)('0' + ((ergebnis[p][bit] >> 1) & 1u)));
            uart_putc(' ');
            uart_putc((uint8_t)('0' + (ergebnis[p][bit] & 1u)));
            uart_puts("\r\n");
        }
    }
    uart_puts("PASS B ende\r\n");
}

void main(void)
{
    uint8_t c;

    ports_all_highz();
    uart_init();

    uart_puts("\r\nSTC8 CENSUS v2 baud=57600\r\n");
    uart_puts("'a' Durchgang A (passiv), 'b' Durchgang B (treibt Pins!)\r\n");
    uart_puts("'R' Neustart in den Bootloader (zum Flashen ohne Stromtrennen)\r\n");

    durchgang_a();
    uart_puts("bereit\r\n");

    for (;;) {
        if (uart_avail()) {
            c = uart_getc();
            if (c == 'R') {
                uart_puts("Neustart in den Bootloader...\r\n");
                ports_all_highz();
                RESET_IN_BOOTLOADER();
                /* does not return */
            }
            if (c == 'a')
                durchgang_a();
            else if (c == 'b')
                durchgang_b();
            else
                uart_puts("? 'a' oder 'b'\r\n");
            uart_puts("bereit\r\n");
        }
    }
}
