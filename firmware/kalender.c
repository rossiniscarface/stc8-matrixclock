/* ---------------------------------------------------------------- Calendar
 *
 * Included in uhr2.c. Entries come from the ESP via the live protocol and
 * live in the EEPROM from sector 1 on (sector 0 holds the settings):
 * 504 slots of 64 bytes, they survive power-off and firmware updates.
 *
 * Entry (64 bytes):
 *   0     state: 0xFF free, 0x01 used, 0x00 deleted (free only after "Y-")
 *   1     month 1-12, 0 = every month
 *   2     day 1-31
 *   3     year 0-99, 0 = every year
 *   4,5   hour, minute; hour 0xFF = all day
 *   6     bit 0: tone at the appointment
 *   7     free
 *   8-63  text, UTF-8, 0-terminated (up to 55 bytes)
 *
 * All-day entries run once per hour as scrolling text in the info rotation
 * after the weekday. Appointments with a time: at the minute (if set)
 * briefly the alarm melody, then the scrolling text.
 *
 * Commands (live protocol):
 *   Y+MMTTJJhhmmfText   entry into the next free slot; hhmm = 9999
 *                       for all day, f = 1 with tone
 *   Y-                  delete all (about 0.3 s, the display stalls meanwhile)
 *   Ydnnn               delete slot nnn
 *   Y?                  number used / free
 *   Ylnnn               show slot nnn
 */
#define KAL_ANFANG   0x0200u
#define KAL_PLATZ    64u
#define KAL_ANZAHL   504u
#define KAL_TEXT     8u
#define KAL_FREI     0xFF
#define KAL_BELEGT   0x01

static uint16_t kal_naechster;  /* search start for the next all-day entry */

static uint16_t kal_adr(uint16_t nr)
{
    return (uint16_t)(KAL_ANFANG + nr * KAL_PLATZ);
}

/* Does slot nr match today? stunde: 0xFF for all day, otherwise the time,
 * which must match exactly. */
static uint8_t kal_passt(uint16_t nr, uint8_t stunde, uint8_t minute)
{
    uint16_t a = kal_adr(nr);
    uint8_t m, j;

    if (iap_lesen(a) != KAL_BELEGT)
        return 0;
    m = iap_lesen(a + 1);
    j = iap_lesen(a + 3);
    if (m && m != bcd((uint8_t)(zeit[5] & 0x1F)))
        return 0;
    if (iap_lesen(a + 2) != bcd(zeit[4]))
        return 0;
    if (j && j != bcd(zeit[6]))
        return 0;
    if (iap_lesen(a + 4) != stunde)
        return 0;
    return (uint8_t)(stunde == 0xFF || iap_lesen(a + 5) == minute);
}

/* start the text of slot nr as scrolling text (non-blocking) */
static void kal_zeigen(uint16_t nr)
{
    uint16_t a = kal_adr(nr) + KAL_TEXT, n = 0;
    uint8_t c;

    while (n < KAL_PLATZ - KAL_TEXT) {
        c = iap_lesen(a + n);
        if (!c || c == 0xFF)
            break;
        textpuffer[n++] = c;
    }
    text_laden(n);
    lauf_off = 0;
    lauf_k = 0;
    lauf_aktiv = 1;
}

/* Info rotation: show the next all-day entry for today from kal_naechster.
 * 1 = started, 0 = none (left). */
static uint8_t kal_ganztags_weiter(void)
{
    uint16_t nr;

    for (nr = kal_naechster; nr < KAL_ANZAHL; nr++) {
        if (kal_passt(nr, 0xFF, 0)) {
            kal_naechster = (uint16_t)(nr + 1);
            kal_zeigen(nr);
            return 1;
        }
    }
    kal_naechster = 0;
    return 0;
}

/* at the minute change: appointments with exactly this time */
static void kal_termine(void)
{
    uint16_t nr;
    uint8_t h = bcd((uint8_t)(zeit[2] & 0x3F)), m = bcd(zeit[1]);

    for (nr = 0; nr < KAL_ANZAHL; nr++) {
        if (kal_passt(nr, h, m)) {
            wecken();
            if (iap_lesen(kal_adr(nr) + 6) & 1)
                melodie_spielen(melodie, 900, 1);   /* about 5 s */
            kal_zeigen(nr);
            return;
        }
    }
}

static uint16_t kal_zahl(uint16_t *frei)
{
    uint16_t nr, belegt = 0;
    uint8_t z;

    *frei = 0;
    for (nr = 0; nr < KAL_ANZAHL; nr++) {
        z = iap_lesen(kal_adr(nr));
        if (z == KAL_BELEGT)
            belegt++;
        else if (z == KAL_FREI)
            (*frei)++;
    }
    return belegt;
}

static uint8_t zwei(void)
{
    uint8_t z = (uint8_t)(ein_getc() - '0');
    return (uint8_t)(z * 10 + (uint8_t)(ein_getc() - '0'));
}

static uint16_t drei(void)
{
    uint16_t v = (uint16_t)(ein_getc() - '0') * 100u;
    v += (uint16_t)(ein_getc() - '0') * 10u;
    return (uint16_t)(v + (uint8_t)(ein_getc() - '0'));
}

/* command 'Y' (the 'Y' has already been read) */
static void kal_befehl(void)
{
    uint8_t op = ein_getc(), kopf[7], c, i;
    uint16_t nr, a, frei;

    if (op == '+') {
        kopf[1] = zwei();                       /* month */
        kopf[2] = zwei();                       /* day */
        kopf[3] = zwei();                       /* year */
        kopf[4] = zwei();                       /* hour, 99 = all day */
        kopf[5] = zwei();
        kopf[6] = (uint8_t)(ein_getc() == '1');
        if (kopf[4] == 99)
            kopf[4] = 0xFF;
        for (nr = 0; nr < KAL_ANZAHL; nr++)
            if (iap_lesen(kal_adr(nr)) == KAL_FREI)
                break;
        if (nr >= KAL_ANZAHL) {
            uart_puts("voll\r\n");
            return;
        }
        a = kal_adr(nr);
        iap_dunkel();                           /* CPU stalls while writing */
        for (i = 1; i < 7; i++)
            iap_schreiben(a + i, kopf[i]);
        for (i = 0; i < KAL_PLATZ - KAL_TEXT - 1; i++) {
            c = ein_getc();
            if (c == '\n' || c == '\r')
                break;
            iap_schreiben(a + KAL_TEXT + i, c);
        }
        iap_schreiben(a + KAL_TEXT + i, 0);
        iap_schreiben(a, KAL_BELEGT);           /* last: only valid now */
        iap_hell();
        uart_puts("platz ");
        dez(nr);
        uart_puts("\r\n");
    } else if (op == '-') {
        iap_dunkel();
        for (a = KAL_ANFANG; a != 0 && a < 0x8000u; a += 512u)
            iap_loeschen(a);
        iap_hell();
        kal_naechster = 0;
        uart_puts("kalender leer\r\n");
    } else if (op == 'd') {
        nr = drei();
        if (nr < KAL_ANZAHL) {
            iap_dunkel();
            iap_schreiben(kal_adr(nr), 0x00);
            iap_hell();
        }
        uart_puts("geloescht\r\n");
    } else if (op == '?') {
        nr = kal_zahl(&frei);
        dez(nr);
        uart_puts(" belegt, ");
        dez(frei);
        uart_puts(" frei\r\n");
    } else if (op == 'l') {
        nr = drei();
        if (nr >= KAL_ANZAHL)
            return;
        a = kal_adr(nr);
        dez(nr);
        uart_putc(' ');
        for (i = 0; i < 7; i++) {
            c = iap_lesen(a + i);
            uart_putc("0123456789ABCDEF"[c >> 4]);
            uart_putc("0123456789ABCDEF"[c & 15]);
            uart_putc(' ');
        }
        for (i = 0; i < KAL_PLATZ - KAL_TEXT; i++) {
            c = iap_lesen(a + KAL_TEXT + i);
            if (!c || c == 0xFF)
                break;
            uart_putc(c);
        }
        uart_puts("\r\n");
    }
}
