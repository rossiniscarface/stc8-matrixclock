/*
 * Checks whether the buzzer hangs off the DS3231 square-wave output instead
 * of the microcontroller.
 *
 * Reason: all six free controller pins stayed silent with constant level
 * and sweep tone. The manual, however, says "If the buzzer is not called,
 * please check whether DS3231 and 9012 Triodes are soldered" — so it makes
 * the CLOCK chip partly responsible for the tone. The DS3231 can output
 * 1 Hz and 1.024 / 4.096 / 8.192 kHz on SQW; via Q1 (PNP 9012) that is
 * enough for a buzzer. Then the sounder does not hang off the controller
 * at all.
 *
 * Control register 0x0E:  bit 2 INTCN (0 = SQW active), bits 4/3 = RS2/RS1
 *   0x00 = 1 Hz, 0x08 = 1.024 kHz, 0x10 = 4.096 kHz, 0x18 = 8.192 kHz
 *
 * Commands:  a  all four frequencies in turn    o  SQW off
 *            R  bootloader
 */
#include "stc8.h"

#define DS3231   0xD0

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


static uint8_t register_schreiben(uint8_t adresse, uint8_t wert)
{
    i2c_init();
    i2c_start();
    if (!i2c_schreiben(DS3231))  { i2c_stop(); return 0; }
    if (!i2c_schreiben(adresse)) { i2c_stop(); return 0; }
    if (!i2c_schreiben(wert))    { i2c_stop(); return 0; }
    i2c_stop();
    return 1;
}

static uint8_t register_lesen(uint8_t adresse)
{
    uint8_t v;

    i2c_init();
    i2c_start();
    i2c_schreiben(DS3231);
    i2c_schreiben(adresse);
    i2c_start();
    i2c_schreiben(DS3231 | 1);
    v = i2c_lesen(0);
    i2c_stop();
    return v;
}

static void pause(uint16_t einheiten)
{
    uint16_t z;
    volatile uint16_t w;

    for (z = 0; z < einheiten; z++)
        for (w = 0; w < 400; w++)
            ;
}

static void hex(uint8_t v)
{
    const uint8_t z[] = "0123456789ABCDEF";

    uart_putc(z[v >> 4]);
    uart_putc(z[v & 0x0F]);
}

void main(void)
{
    uint8_t c;

    ports_all_highz();
    uart_init();
    uart_puts("\r\nSTC8 SQW v1 - Buzzer am DS3231-Rechteckausgang?\r\n");
    uart_puts("Steuerregister 0x0E steht auf 0x");
    hex(register_lesen(0x0E));
    uart_puts("\r\n'a' = 1 Hz, 1k, 4k, 8k nacheinander. 'o' aus, 'R' Bootloader\r\n");

    for (;;) {
        if (!uart_avail())
            continue;
        c = uart_getc();

        if (c == 'R') {
            uart_puts("Neustart...\r\n");
            register_schreiben(0x0E, 0x04);     /* SQW off */
            ports_all_highz();
            RESET_IN_BOOTLOADER();
        } else if (c == 'o') {
            uart_puts(register_schreiben(0x0E, 0x04) ? "SQW aus\r\n" : "kein ACK\r\n");
        } else if (c == 'a') {
            uart_puts("1 Hz\r\n");      register_schreiben(0x0E, 0x00); pause(5000);
            uart_puts("1,024 kHz\r\n");  register_schreiben(0x0E, 0x08); pause(5000);
            uart_puts("4,096 kHz\r\n");  register_schreiben(0x0E, 0x10); pause(5000);
            uart_puts("8,192 kHz\r\n");  register_schreiben(0x0E, 0x18); pause(5000);
            register_schreiben(0x0E, 0x04);
            uart_puts("fertig, SQW wieder aus\r\n");
        }
    }
}
