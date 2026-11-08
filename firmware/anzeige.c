/* --------------------------------------------------------- Display sequence
 *
 * Included in uhr2.c (#include) so that everything stays in one translation
 * unit and the drawing functions are directly reachable.
 *
 * anzeige_tick() runs in every frame:
 *   - reads the clock every 18 frames (about 100 ms) and notes the second
 *     change (for the colon effects),
 *   - switches the screens: time for the info interval, then date and
 *     weekday about 3 s each (the temperature is measured by the DS3231 on
 *     the chip and has little to do with room temperature - only available
 *     via command 'T' now),
 *   - draws the target image invisibly into ziel[] and brings it to bild[]
 *     with a transition animation (new screen) or a digit effect (changed
 *     digits of the time).
 *
 * Transition animations (setting wechsel):
 *   0 off (direct change), 1 push left, 2 random pixels,
 *   3 column by column, 4 top to bottom, 5 split from the centre,
 *   6 roll centre, 7 roll up and down, 8 wipe sideways.
 * Info rotation (info_int): 0..4 = 5/10/30/60/120 s, 5 = off (time only).
 * Digit effects (ziffer_eff): 0 direct, 1 build column by column,
 *   2 fall, 3 random pixels, 4 up and down, 5 from the centre.
 * Colon (dpunkt): 1 run to the centre, 2 fade in and out,
 *   3 constant, 4 move up and down, 5 blink every second.
 */

#define S_ZEIT   0
#define S_DATUM  1
#define S_TAG    2
#define S_COUNT  4              /* running countdown */
#define S_KAL    5              /* calendar scrolling text in the info rotation */
#define INFO_DAUER   535u       /* each info about 3 s */
#define RAHMEN_SEK   178u       /* frames per second (5.6 ms) */

static __xdata uint8_t ziel[SPALTEN], alt[SPALTEN], ziel_vorher[SPALTEN];
static __xdata uint8_t zf_maske[SPALTEN];
static uint8_t schirm = S_ZEIT, schirm_vorher = 0xFF;
static uint16_t schirm_rahmen;
static uint8_t tr_typ, tr_schritt, tr_n, tr_takt;
static uint8_t zf_schritt, zf_n, zf_takt;
static uint8_t sek_alt = 0xFF, rtc_takt, rahmen_zaehler;
static uint16_t sek_phase;
static uint8_t minute_neu;      /* second 0 reached: check appointments */
static uint8_t kal_stunde = 0xFF;   /* all-day entries: once per hour */

static uint8_t kal_ganztags_weiter(void);   /* kalender.c */

static const __code uint8_t INFO_SEK[5] = { 5, 10, 30, 60, 120 };
/* steps and frames per step of the transition animations 0..9 */
static const __code uint8_t TR_N[10]    = { 0, 26, 20, 25, 7, 13, 8, 7, 26, 0 };
static const __code uint8_t TR_TAKT[10] = { 0, 2, 3, 2, 6, 4, 5, 6, 2, 0 };
static const __code uint8_t ZF_N[6]     = { 0, 0, 14, 12, 7, 8 };   /* 1: by width */

static uint16_t zufall_wert = 0x1234;

static uint8_t zufall(void)
{
    zufall_wert = (uint16_t)(zufall_wert * 25173u + 13849u + TL0 + frames_gesamt);
    return (uint8_t)(zufall_wert >> 8);
}

/* Order for "random pixels": each of the 175 pixels gets a fixed rank
 * 0..174 (multiplication by 37 modulo 175 is a permutation). */
static uint8_t pixel_rang(uint8_t x, uint8_t y)
{
    return (uint8_t)(((uint16_t)(y * SPALTEN + x) * 37u) % 175u);
}

/* ------------------------------------------------------------------ Colon */

static uint8_t dp_muster(uint8_t gross_)
{
    uint8_t q = (uint8_t)(sek_phase >= RAHMEN_SEK ? 7 : (sek_phase * 8u) / RAHMEN_SEK);
    uint8_t basis = (uint8_t)(gross_ ? DP2 : DOPPELPUNKT);
    static const __code uint8_t KONV_K[4] = { 0x22, 0x14, 0x08, 0x14 };
    static const __code uint8_t KONV_G[4] = { 0x63, 0x36, 0x1C, 0x36 };
    static const __code uint8_t AUFAB_K[4] = { 0x0A, 0x14, 0x28, 0x14 };
    static const __code uint8_t AUFAB_G[4] = { 0x1B, 0x36, 0x6C, 0x36 };

    switch (dpunkt) {
    case 1: return gross_ ? KONV_G[q >> 1] : KONV_K[q >> 1];
    case 2: return basis;               /* brightness is done by dp_dimmen() in the interrupt */
    case 3: return basis;
    case 4: return gross_ ? AUFAB_G[q >> 1] : AUFAB_K[q >> 1];
    default:
        if (q < 4)
            return basis;
        return (uint8_t)(gross_ ? 0x08 : 0x00);   /* theme 2: centre keeps glowing */
    }
}

#define T3_TRENNER 8             /* theme 3: colon free between HH and MM */

static uint8_t ist_trenner(uint8_t x)
{
    if (theme == 2)
        return (uint8_t)(x == Z2_T);
    if (theme == 3)
        return (uint8_t)(x == T3_TRENNER);
    return (uint8_t)(x == TRENNER || x == TRENNER2);
}

/* ---------------------------------------------------------------- Screens */

static void buchstabe(uint8_t ab, uint8_t b)
{
    zb[ab] = BUCHSTABE[b][0];
    zb[ab + 1] = BUCHSTABE[b][1];
    zb[ab + 2] = BUCHSTABE[b][2];
}

/* weekday in the 5x7 font of the scrolling text, centred (as requested by the user) */
static const __code char TAG_TEXT[7][4] = { "Mon", "Die", "Mit", "Don", "Fre", "Sam", "Son" };

static void tag_zeichnen(void)
{
    uint8_t w = (uint8_t)((zeit[3] & 7) - 1), i, ab;

    if (w > 6)
        w = 6;
    for (i = 0; i < 3; i++)                 /* via the stream, so that the */
        textpuffer[i] = (uint8_t)TAG_TEXT[w][i];  /* Schrift setting applies */
    text_laden(3);
    leeren();
    ab = (uint8_t)(strom_len < SPALTEN ? (SPALTEN - strom_len) / 2 : 0);
    for (i = 0; i < strom_len && ab + i < SPALTEN; i++)
        zb[ab + i] = strom[i];
}

/* theme 3: hours and minutes large (3x7), seconds small (3x5, bottom) */
static void anzeige_t3(uint8_t a, uint8_t b, uint8_t c, uint8_t trenner)
{
    leeren();
    gross(0, (uint8_t)(a / 10));
    gross(4, (uint8_t)(a % 10));
    zb[T3_TRENNER] = trenner;
    gross(10, (uint8_t)(b / 10));
    gross(14, (uint8_t)(b % 10));
    klein(18, (uint8_t)(c / 10));
    klein(22, (uint8_t)(c % 10));
}

static void schirm_zeichnen(uint8_t s)
{
    uint8_t h;

    if (s == S_COUNT) {                 /* MM:SS in the wide digits */
        anzeige_t2((uint8_t)(count_rest / 60 > 99 ? 99 : count_rest / 60),
                   (uint8_t)(count_rest % 60), DOPPELPUNKT);
        return;
    }
    if (s == S_TAG) {
        tag_zeichnen();
        return;
    }
    if (s == S_DATUM) {
        if (theme == 2)
            anzeige_t2(bcd(zeit[4]), bcd(zeit[5] & 0x1F), PUNKT);
        else
            anzeige_t1(bcd(zeit[4]), bcd(zeit[5] & 0x1F), bcd(zeit[6]), PUNKT);
        return;
    }
    h = zeit[2];
    h = (h & 0x40) ? bcd((uint8_t)(h & 0x1F)) : bcd((uint8_t)(h & 0x3F));
    if (theme == 2)
        anzeige_t2(h, bcd(zeit[1]), dp_muster(1));
    else if (theme == 3)
        anzeige_t3(h, bcd(zeit[1]), bcd(zeit[0]), dp_muster(0));
    else
        anzeige_t1(h, bcd(zeit[1]), bcd(zeit[0]), dp_muster(0));
    if (theme == 2)
        zb[Z2_T] = dp_muster(1);    /* anzeige_t2 only replaces DOPPELPUNKT */
}

/* ------------------------------------------------------------- Animations */

/* Column between old (a) and new (n) when squashing from/to the middle row:
 * steps 0..h-1 shrink a, h..2h-1 grow n. */
static uint8_t squash(uint8_t a, uint8_t n, uint8_t s, uint8_t h)
{
    static const __code uint8_t BAND[4] = { 0x7F, 0x3E, 0x1C, 0x08 };

    if (s < h)
        return (uint8_t)(a & BAND[s & 3]);
    return (uint8_t)(n & BAND[(uint8_t)(2 * h - 1 - s) & 3]);
}

static uint8_t tr_spalte(uint8_t x, uint8_t s)
{
    uint8_t a = alt[x], n = ziel[x], y, m, v;
    int8_t d;

    switch (tr_typ) {
    case 1:                                     /* push left */
        v = (uint8_t)(x + s);
        if (v < SPALTEN) return alt[v];
        if (v == SPALTEN) return 0;
        return ziel[v - SPALTEN - 1];
    case 2:                                     /* random pixels */
        v = 0;
        for (y = 0; y < 7; y++) {
            m = (uint8_t)(1u << y);
            v |= (uint8_t)((pixel_rang(x, y) < (uint16_t)s * 175u / TR_N[2]) ? (n & m) : (a & m));
        }
        return v;
    case 3:                                     /* column by column */
        return x < s ? n : a;
    case 4:                                     /* top to bottom */
        m = (uint8_t)((1u << s) - 1);
        return (uint8_t)((n & m) | (a & (uint8_t)~m));
    case 5:                                     /* split from the centre */
        d = (int8_t)(x - 12);
        if (d < 0) d = (int8_t)-d;
        return (uint8_t)d < s ? n : a;
    case 6:                                     /* roll centre */
        return squash(a, n, s, 4);
    case 7:                                     /* roll up and down */
        return (uint8_t)(((a >> s) | (n << (7 - s))) & 0x7F);
    case 8:                                     /* wipe sideways */
        if (x + 1 < s) return n;
        if (x + 1 == s) return 0x7F;
        return a;
    default:
        return n;
    }
}

static uint8_t zf_spalte(uint8_t x, uint8_t s, uint8_t rang)
{
    uint8_t a = alt[x], n = ziel[x], y, m, v;

    switch (ziffer_eff) {
    case 1:                                     /* build column by column */
        return rang < s ? n : 0;
    case 2:                                     /* fall: old falls out, new falls in */
        if (s < 7) return (uint8_t)((a << (s + 1)) & 0x7F);
        return (uint8_t)(n >> (13 - s));
    case 3:                                     /* random pixels */
        v = 0;
        for (y = 0; y < 7; y++) {
            m = (uint8_t)(1u << y);
            v |= (uint8_t)((pixel_rang(x, y) < (uint16_t)s * 175u / ZF_N[3]) ? (n & m) : (a & m));
        }
        return v;
    case 4:                                     /* up and down */
        return (uint8_t)(((a >> s) | (n << (7 - s))) & 0x7F);
    case 5:                                     /* from the centre */
        return squash(a, n, s, 4);
    default:
        return n;
    }
}

/* --------------------------------------------------------------- Sequence */

static uint8_t naechster_schirm(uint8_t s)
{
    return (uint8_t)(s >= S_TAG ? S_ZEIT : s + 1);
}

static void countdown_start(uint16_t sek)
{
    count_rest = sek;
    count_aktiv = (uint8_t)(sek != 0);
    count_ende = 0;
    schirm = count_aktiv ? S_COUNT : S_ZEIT;
    schirm_rahmen = 0;
}

/* Draw immediately, without animation - after menu, alarm, scrolling text. */
static void anzeige_sofort(void)
{
    uint8_t x;

    rtc_lesen(0x00, zeit, 7);
    zb = ziel;
    schirm_zeichnen(schirm);
    zb = bild;
    for (x = 0; x < SPALTEN; x++) {
        bild[x] = ziel[x];
        ziel_vorher[x] = ziel[x];
    }
    schirm_vorher = schirm;
    tr_n = 0;
    zf_n = 0;
}

/* touch of the triangle: next screen */
static void anzeige_weiter(void)
{
    schirm = naechster_schirm(schirm);
    schirm_rahmen = 0;
}

/* Read the clock, called every frame - also during scrolling text and
 * light organ, so that countdown and appointments keep running. */
static void uhr_tick(void)
{
    rahmen_zaehler++;
    if (sek_phase < 0xFFFF)
        sek_phase++;
    if (++rtc_takt >= 18) {
        rtc_takt = 0;
        if (rtc_lesen(0x00, zeit, 7) && zeit[0] != sek_alt) {
            sek_alt = zeit[0];
            sek_phase = 0;
            if (count_aktiv && count_rest && --count_rest == 0)
                count_ende = 1;             /* main loop is ringing */
            if (zeit[0] == 0)
                minute_neu = 1;
        }
    }
}

static void anzeige_tick(void)
{
    uint8_t x, n, rang;

    schirm_rahmen++;
    if (count_aktiv) {
        schirm = S_COUNT;
    } else if (schirm == S_COUNT) {
        schirm = S_ZEIT;
    } else if (schirm == S_ZEIT) {
        if (info_int < 5 && schirm_rahmen >= (uint16_t)INFO_SEK[info_int] * RAHMEN_SEK) {
            schirm = S_DATUM;
            schirm_rahmen = 0;
        }
    } else if (schirm_rahmen >= INFO_DAUER) {
        schirm_rahmen = 0;
        if (schirm == S_TAG && kal_stunde != zeit[2]
            && (kal_stunde = zeit[2], kal_ganztags_weiter())) {
            schirm = S_KAL;                 /* scrolling text running, see main loop */
            schirm_vorher = S_KAL;
            return;
        }
        schirm = naechster_schirm(schirm);
    }

    zb = ziel;
    schirm_zeichnen(schirm);
    zb = bild;

    if (schirm != schirm_vorher) {
        for (x = 0; x < SPALTEN; x++)
            alt[x] = bild[x];
        tr_typ = (uint8_t)(wechsel ? wechsel : 9);
        tr_n = TR_N[tr_typ];
        tr_schritt = 0;
        tr_takt = 0;
        zf_n = 0;
        schirm_vorher = schirm;
    } else if (!tr_n && !zf_n && schirm == S_ZEIT && ziffer_eff) {
        n = 0;
        for (x = 0; x < SPALTEN; x++) {
            zf_maske[x] = (uint8_t)(ziel[x] != ziel_vorher[x] && !ist_trenner(x));
            n |= zf_maske[x];
        }
        if (n) {
            for (x = 0; x < SPALTEN; x++)
                alt[x] = bild[x];
            zf_n = ZF_N[ziffer_eff];
            if (ziffer_eff == 1) {              /* as many steps as columns */
                for (x = 0; x < SPALTEN; x++)
                    zf_n += zf_maske[x];
                zf_n++;
            }
            zf_schritt = 1;
            zf_takt = 0;
        }
    }
    for (x = 0; x < SPALTEN; x++)
        ziel_vorher[x] = ziel[x];

    /* Fade the colon: 8 levels up and down again per second, only while the
     * time stands still (otherwise other columns would be dimmed) */
    if (dpunkt == 2 && schirm == S_ZEIT && !tr_n) {
        n = (uint8_t)(sek_phase >= RAHMEN_SEK ? 0 : (sek_phase * 16u) / RAHMEN_SEK);
        n = (uint8_t)(n < 8 ? n + 1 : 16 - n);
        if (theme == 2)
            dp_dimmen(Z2_T, 0xFF, n);
        else if (theme == 3)
            dp_dimmen(T3_TRENNER, 0xFF, n);
        else
            dp_dimmen(TRENNER, TRENNER2, n);
    } else if (dp_s1 != 0xFF) {
        dp_dimmen(0xFF, 0xFF, 8);
    }

    if (tr_n) {
        for (x = 0; x < SPALTEN; x++)
            bild[x] = tr_spalte(x, tr_schritt);
        if (++tr_takt >= TR_TAKT[tr_typ]) {
            tr_takt = 0;
            if (++tr_schritt > tr_n)
                tr_n = 0;
        }
        return;
    }
    if (zf_n) {
        rang = 0;
        for (x = 0; x < SPALTEN; x++) {
            if (zf_maske[x]) {
                bild[x] = zf_spalte(x, zf_schritt, rang);
                rang++;
            } else {
                bild[x] = ziel[x];
            }
        }
        if (++zf_takt >= 3) {
            zf_takt = 0;
            if (++zf_schritt > zf_n)
                zf_n = 0;
        }
        return;
    }
    for (x = 0; x < SPALTEN; x++)
        bild[x] = ziel[x];
}

/* Menu Anim: demonstrate the selected animation - from the menu text to the
 * time, stand for 1 s, back with the same animation. */
static void anim_lauf(void)
{
    uint8_t x, s, k;

    tr_typ = wechsel;
    for (s = 0; s <= TR_N[tr_typ]; s++) {
        for (x = 0; x < SPALTEN; x++)
            bild[x] = tr_spalte(x, s);
        for (k = 0; k < TR_TAKT[tr_typ]; k++)
            rahmen();
    }
}

static void anim_vorfuehren(void)
{
    uint8_t x;

    if (!wechsel)
        return;
    for (x = 0; x < SPALTEN; x++) {
        alt[x] = bild[x];
        ziel_vorher[x] = bild[x];           /* remember menu text */
    }
    rtc_lesen(0x00, zeit, 7);
    zb = ziel;
    schirm_zeichnen(S_ZEIT);
    zb = bild;
    anim_lauf();
    for (x = 0; x < 180; x++)
        rahmen();
    for (x = 0; x < SPALTEN; x++) {
        alt[x] = bild[x];
        ziel[x] = ziel_vorher[x];
    }
    anim_lauf();
}
