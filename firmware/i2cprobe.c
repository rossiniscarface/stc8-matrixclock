/*
 * Searches the I2C bus to the DS3231.
 *
 * Pass B found five pins with an external pull-up: P1.6, P4.3, P4.4, P5.4,
 * P5.5. None of them carries the STC8's hardware I2C function, so the bus
 * is bit-banged in software. Which two pins it is cannot be guessed — but
 * the DS3231 can answer the question itself: it acknowledges its address
 * 0x68 with an ACK.
 *
 * So all 20 ordered pairs are tried. Exactly one should answer. If several
 * ACK, something is wrong; if none does, the bus is elsewhere and the
 * candidate list was too narrow.
 *
 * Open-drain is exactly right here: latch 0 pulls the pin low, latch 1
 * leaves it to the external pull-up — that is I2C behaviour.
 */
#include "stc8.h"

#define N_KAND 5

static const uint8_t KAND_PORT[N_KAND] = { 1, 4, 4, 5, 5 };
static const uint8_t KAND_BIT[N_KAND]  = { 6, 3, 4, 4, 5 };

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
        latch[3] |= 0x03;          /* never touch the UART */
    port_write(p, latch[p]);
}

static uint8_t pin_get(uint8_t p, uint8_t bit)
{
    return (uint8_t)((port_read(p) >> bit) & 1u);
}

static void halt(void)
{
    volatile uint8_t i;
    for (i = 0; i < 40; i++)
        ;
}

/* --- bit-bang I2C on a freely chosen pin pair --- */

static uint8_t scl_p, scl_b, sda_p, sda_b;

static void scl(uint8_t v) { pin_set(scl_p, scl_b, v); halt(); }
static void sda(uint8_t v) { pin_set(sda_p, sda_b, v); halt(); }

static void i2c_start(void)
{
    sda(1); scl(1);
    sda(0);
    scl(0);
}

static void i2c_stop(void)
{
    sda(0); scl(1);
    sda(1);
}

/* Sends a byte, returns 1 if the slave acknowledged. */
static uint8_t i2c_write(uint8_t v)
{
    uint8_t i, ack;

    for (i = 0; i < 8; i++) {
        sda((uint8_t)((v & 0x80) ? 1 : 0));
        scl(1);
        scl(0);
        v = (uint8_t)(v << 1);
    }
    sda(1);                         /* release the line, slave may pull */
    scl(1);
    ack = (uint8_t)(pin_get(sda_p, sda_b) == 0);
    scl(0);
    return ack;
}

static uint8_t teste_paar(uint8_t a, uint8_t b)
{
    uint8_t ack;

    scl_p = KAND_PORT[a]; scl_b = KAND_BIT[a];
    sda_p = KAND_PORT[b]; sda_b = KAND_BIT[b];

    pin_mode(scl_p, scl_b, 3);      /* open drain */
    pin_mode(sda_p, sda_b, 3);
    pin_set(scl_p, scl_b, 1);
    pin_set(sda_p, sda_b, 1);
    halt();

    i2c_start();
    ack = i2c_write(0xD0);          /* 0x68 << 1, write access */
    i2c_stop();

    pin_mode(scl_p, scl_b, 2);      /* high-impedance again */
    pin_mode(sda_p, sda_b, 2);
    return ack;
}

static void namen(uint8_t k)
{
    uart_putc('P');
    uart_putc((uint8_t)('0' + KAND_PORT[k]));
    uart_putc('.');
    uart_putc((uint8_t)('0' + KAND_BIT[k]));
}

static void durchlauf(void)
{
    uint8_t a, b, ack, treffer = 0;

    uart_puts("I2C-Suche auf Adresse 0x68 (DS3231)\r\n");
    for (a = 0; a < N_KAND; a++) {
        for (b = 0; b < N_KAND; b++) {
            if (a == b)
                continue;
            ack = teste_paar(a, b);
            if (ack) {
                treffer++;
                uart_puts("  ACK  SCL=");
                namen(a);
                uart_puts("  SDA=");
                namen(b);
                uart_puts("\r\n");
            }
        }
    }
    if (!treffer)
        uart_puts("  kein ACK - Bus liegt nicht auf diesen fuenf Pins\r\n");
    uart_puts("I2C-Suche ende\r\n");
}

void main(void)
{
    uint8_t c;

    ports_all_highz();
    uart_init();
    uart_puts("\r\nSTC8 I2CPROBE v1 baud=57600\r\n");
    uart_puts("'i' Suche starten, 'R' Neustart in den Bootloader\r\n");

    durchlauf();
    uart_puts("bereit\r\n");

    for (;;) {
        if (uart_avail()) {
            c = uart_getc();
            if (c == 'R') {
                uart_puts("Neustart...\r\n");
                ports_all_highz();
                RESET_IN_BOOTLOADER();
            }
            if (c == 'i')
                durchlauf();
            else
                uart_puts("? 'i'\r\n");
            uart_puts("bereit\r\n");
        }
    }
}
