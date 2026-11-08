/*
 * Read the time from the DS3231 and show it on the matrix.
 *
 * Assumes the measured pinout (see docs/PINOUT.md):
 *   rows    (source, high): P2.0 … P2.6, P2.0 is at the top
 *   columns (sink,   low ): P3.0-P3.7, P0.0-P0.7, P1.0-P1.5, P4.0-P4.2,
 *                           P3.0 is on the left
 *   I2C to the DS3231: P5.4 and P5.5
 *
 * Three things that determine the structure:
 *
 *  - While the display runs, P3.0 and P3.1 are matrix columns. The UART is
 *    then dead. So the firmware blanks briefly three times per second,
 *    restores the UART and listens — otherwise the device could no longer
 *    be reflashed via software reset, and unplugging would be back.
 *
 *  - Scanning is column-wise, one column low. The other way round more than
 *    three times as much current would flow through the one row pin; the
 *    matrix has no series resistors.
 *
 *  - Which of the two I2C lines is SCL is not taken on faith but tried at
 *    start-up: the assignment to which the DS3231 answers with an
 *    acknowledgement wins.
 *
 * Commands (only in the listening window or before the start):
 *   o  halt      R  restart into the bootloader
 */
#include "stc8.h"

#define SPALTEN   25
#define RAHMEN   220            /* per display block, about 200 ms */
#define DS3231   0xD0

static const uint8_t SPALTE[SPALTEN] = {
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x40, 0x41, 0x42
};

/* 5x7 font, column-wise, bit 0 is at the top. */
static const uint8_t ZIFFER[10][5] = {
    { 0x3E, 0x51, 0x49, 0x45, 0x3E }, { 0x00, 0x42, 0x7F, 0x40, 0x00 },
    { 0x42, 0x61, 0x51, 0x49, 0x46 }, { 0x21, 0x41, 0x45, 0x4B, 0x31 },
    { 0x18, 0x14, 0x12, 0x7F, 0x10 }, { 0x27, 0x45, 0x45, 0x45, 0x39 },
    { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, { 0x01, 0x71, 0x09, 0x05, 0x03 },
    { 0x36, 0x49, 0x49, 0x49, 0x36 }, { 0x06, 0x49, 0x49, 0x29, 0x1E }
};

static __xdata uint8_t bild[SPALTEN];
static uint8_t latch[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

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

/* Write seconds, minutes, hours. Bit 6 of the hours register stays 0,
 * which is 24-hour mode on the DS3231. */
static uint8_t zeit_stellen(uint8_t std, uint8_t min, uint8_t sek)
{
    i2c_init();
    i2c_start();
    if (!i2c_schreiben(DS3231))      { i2c_stop(); return 0; }
    if (!i2c_schreiben(0x00))        { i2c_stop(); return 0; }
    if (!i2c_schreiben(zubcd(sek)))  { i2c_stop(); return 0; }
    if (!i2c_schreiben(zubcd(min)))  { i2c_stop(); return 0; }
    if (!i2c_schreiben(zubcd(std)))  { i2c_stop(); return 0; }
    i2c_stop();
    return 1;
}

static uint8_t bcd(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

/* ------------------------------------------------------------- Display */

static void port_write(uint8_t p, uint8_t v)
{
    switch (p) {
    case 0: P0 = v; break;
    case 1: P1 = v; break;
    case 3: P3 = v; break;
    default: P4 = v; break;
    }
}

static void spalte_an(uint8_t p, uint8_t b)
{
    uint8_t m = (uint8_t)(1u << b);

    latch[p] = (uint8_t)(latch[p] & ~m);
    port_write(p, latch[p]);
    switch (p) {
    case 0: P0M1 &= (uint8_t)~m; P0M0 |= m; break;
    case 1: P1M1 &= (uint8_t)~m; P1M0 |= m; break;
    case 3: P3M1 &= (uint8_t)~m; P3M0 |= m; break;
    default: P4M1 &= (uint8_t)~m; P4M0 |= m; break;
    }
}

static void spalte_aus(uint8_t p, uint8_t b)
{
    uint8_t m = (uint8_t)(1u << b);

    switch (p) {
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

    P2M1 &= 0x80;                       /* P2.0…P2.6 push-pull, */
    P2M0 |= 0x7F;                       /* P2.7 stays high-impedance */

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

static void ziffer(uint8_t ab, uint8_t z)
{
    uint8_t i;

    for (i = 0; i < 5; i++)
        bild[ab + i] = ZIFFER[z][i];
}

/* 5 + 1 + 5 + 1 + 1 + 1 + 5 + 1 + 5 = 25 columns, the colon gets space on
 * both sides. */
static void bild_bauen(uint8_t std, uint8_t min, uint8_t punkt)
{
    uint8_t i;

    for (i = 0; i < SPALTEN; i++)
        bild[i] = 0;
    ziffer(0, (uint8_t)(std / 10));
    ziffer(6, (uint8_t)(std % 10));
    if (punkt)
        bild[12] = 0x14;
    ziffer(14, (uint8_t)(min / 10));
    ziffer(20, (uint8_t)(min % 10));
}

static void striche(void)
{
    uint8_t i;

    for (i = 0; i < SPALTEN; i++)
        bild[i] = (uint8_t)((i % 3) ? 0x08 : 0x00);
}

/* ----------------------------------------------------------------- Run */

static uint8_t zweistellig(void)
{
    uint8_t z, e;

    z = uart_getc();
    e = uart_getc();
    return (uint8_t)((z - '0') * 10 + (e - '0'));
}

static void zahl(uint8_t v)
{
    uart_putc((uint8_t)('0' + v / 10));
    uart_putc((uint8_t)('0' + v % 10));
}

int main(void)
{
    uint8_t std, min, sek, h, c, weiter;
    uint16_t lausch;

    ports_all_highz();
    uart_init();
    uart_puts("\r\nSTC8 UHR v1\r\n");

    /* Which line is SCL is tried instead of taken on faith. */
    if (antwortet()) {
        uart_puts("DS3231 antwortet: SCL=P5.4 SDA=P5.5\r\n");
    } else {
        SCL_M = 0x20; SDA_M = 0x10;
        if (antwortet())
            uart_puts("DS3231 antwortet: SCL=P5.5 SDA=P5.4 (vertauscht)\r\n");
        else
            uart_puts("DS3231 antwortet nicht - Anzeige zeigt Striche\r\n");
    }

    if (zeit_lesen()) {
        uart_puts("Uhrzeit ");
        h = roh[2];
        std = (h & 0x40) ? bcd((uint8_t)(h & 0x1F)) : bcd((uint8_t)(h & 0x3F));
        zahl(std); uart_putc(':'); zahl(bcd(roh[1]));
        uart_putc(':'); zahl(bcd(roh[0])); uart_puts("\r\n");
    }
    uart_puts("Anzeige laeuft. 'o' haelt an, 'R' startet in den Bootloader.\r\n");

    for (;;) {
        if (zeit_lesen()) {
            h = roh[2];
            std = (h & 0x40) ? bcd((uint8_t)(h & 0x1F)) : bcd((uint8_t)(h & 0x3F));
            min = bcd(roh[1]);
            sek = bcd(roh[0]);
            bild_bauen(std, min, (uint8_t)(sek & 1));
        } else {
            striche();
        }

        zeigen(RAHMEN);

        /* Blanked, UART back: listen briefly. Without this window the device
         * could only be reflashed by unplugging. */
        uart_init();
        /* about 60 ms per 200 ms of display, i.e. just under a quarter of the
         * time. isp_reset.sh sends an 'R' every 50 ms for 0.8 s — so on
         * average four characters hit a window. It must not take longer,
         * otherwise the garbage drowns out the bootloader handshake. The
         * display gets a quarter darker as a result; at a measured 224 of
         * 255 there is plenty of headroom for that. */
        for (lausch = 0; lausch < 65000u; lausch++) {
            if (!uart_avail())
                continue;
            c = uart_getc();
            /* EVERY character halts, not just 'o'. Reason: after a reset the
             * bootloader waits only about 0.2 s for the handshake. Restarting
             * immediately here forces the flash tool to hit exactly this
             * window — while the sender is still spreading its characters
             * just to catch a listening window at all. So halt first and
             * listen continuously; the restart then comes with the next 'R'
             * at a time the sender knows. */
            (void)c;
            ports_all_highz();
            uart_init();
            uart_puts("angehalten. 'sHHMMSS' stellt die Zeit, "
                      "'w' zeigt weiter, 'R' startet in den Bootloader.\r\n");
            weiter = 0;
            while (!weiter) {
                if (!uart_avail())
                    continue;
                c = uart_getc();
                if (c == 'R') {
                    uart_puts("Neustart...\r\n");
                    RESET_IN_BOOTLOADER();
                } else if (c == 'w') {
                    uart_puts("weiter\r\n");
                    weiter = 1;
                } else if (c == 's') {
                    std = zweistellig();
                    min = zweistellig();
                    sek = zweistellig();
                    if (std > 23 || min > 59 || sek > 59)
                        uart_puts("unsinnige Zeit\r\n");
                    else if (zeit_stellen(std, min, sek))
                        uart_puts("gestellt\r\n");
                    else
                        uart_puts("DS3231 antwortet nicht\r\n");
                }
            }
            break;
        }
    }
}
