/* Digital twin - hardware replacement and script.
 *
 * Usage:  ZW_SKRIPT=drehbuch.txt ZW_AUSGABE=lauf.jsonl ./uhr2_pc
 *
 * Script (Drehbuch), one instruction per line, time in seconds from start:
 *   zeit 07:59:50          start time of the DS3231
 *   datum 08.11.26 7       start date and weekday (1 = Monday)
 *   licht 3650             light sensor (ADC 6; dark = high)
 *   @1.0 kreis 0.2         touch circle for 0.2 s   (dreieck / beide likewise)
 *   @5 befehl t2           live command, as via the protocol
 *   @6 laut 0.1            noise for 0.1 s (microphone)
 *   @7 licht 4090          change light sensor
 *   @30 ende               end simulation
 *   # comment
 *
 * Output: JSON lines {"t":ms,"b":"25 column bytes hex"} on every change of
 * the image, plus {"t":ms,"ton":"4k"} and {"t":ms,"uart":"..."}. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SPALTEN    25
#define RAHMEN_MS  5.6          /* measured with 5 listening pauses */

uint8_t zw_iap[32768];

static FILE *aus;
static double jetzt_ms;         /* simulation time */
static unsigned long rahmen_nr;
static uint8_t letztes[SPALTEN];
static int erstes = 1;

/* script */
typedef struct { double t; char art[16]; char arg[96]; } Ereignis;
static Ereignis ev[4096];
static int ev_n, ev_i;

/* state of the simulated hardware */
static double kreis_bis = -1, dreieck_bis = -1, laut_bis = -1;
static uint16_t licht = 3650;
static uint8_t rtc[0x13];
static long sek_basis = 0;      /* seconds since midnight at jetzt_ms = 0 */
static long letzte_sek = -1;
static const char *ton_jetzt = "aus";

/* live commands as a byte sequence like in the ring buffer: 0xFF, line, check character, \n */
static uint8_t rx[1024];
static int rx_lesen_i, rx_schreib_i;

static uint8_t bcd(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 15)); }
static uint8_t zubcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static void drehbuch_laden(void)
{
    const char *pfad = getenv("ZW_SKRIPT");
    char z[160];
    FILE *f;
    double t = 0;

    memset(zw_iap, 0xFF, sizeof zw_iap);
    rtc[0x0E] = 0x04;
    rtc[0x11] = 21; rtc[0x12] = 0x40;            /* 21.25 degrees */
    rtc[0x03] = 7; rtc[0x04] = 0x08; rtc[0x05] = 0x11; rtc[0x06] = 0x26;
    sek_basis = 12 * 3600 + 34 * 60 + 56;
    rtc[0x0B] = 1;
    aus = fopen(getenv("ZW_AUSGABE") ? getenv("ZW_AUSGABE") : "lauf.jsonl", "w");
    if (!pfad || !(f = fopen(pfad, "r"))) {
        fprintf(stderr, "zwilling: no script (ZW_SKRIPT)\n");
        exit(1);
    }
    while (fgets(z, sizeof z, f)) {
        char *p = z, art[16] = "", arg[96] = "";
        z[strcspn(z, "\r\n")] = 0;
        while (*p == ' ') p++;
        if (!*p || *p == '#') continue;
        if (*p == '@') { t = atof(p + 1); p = strchr(p, ' '); if (!p) continue; p++; }
        sscanf(p, "%15s %95[^\n]", art, arg);
        if (!strcmp(art, "zeit") && t == 0) {
            int h, m, s;
            sscanf(arg, "%d:%d:%d", &h, &m, &s);
            sek_basis = h * 3600 + m * 60 + s;
            continue;
        }
        if (!strcmp(art, "datum") && t == 0) {
            int d, mo, j, w = 7;
            sscanf(arg, "%d.%d.%d %d", &d, &mo, &j, &w);
            rtc[4] = zubcd(d); rtc[5] = zubcd(mo); rtc[6] = zubcd(j); rtc[3] = (uint8_t)w;
            continue;
        }
        if (!strcmp(art, "licht") && t == 0) { licht = (uint16_t)atoi(arg); continue; }
        ev[ev_n].t = t;
        strcpy(ev[ev_n].art, art);
        strcpy(ev[ev_n].arg, arg);
        ev_n++;
    }
    fclose(f);
}

static void befehl_einreihen(const char *b)
{
    int i, sum = 0;
    rx[rx_schreib_i++ & 1023] = 0xFF;
    for (i = 0; b[i]; i++) { rx[rx_schreib_i++ & 1023] = (uint8_t)b[i]; sum += (uint8_t)b[i]; }
    rx[rx_schreib_i++ & 1023] = (uint8_t)(0x40 + (sum & 0x3F));
    rx[rx_schreib_i++ & 1023] = '\n';
}

static void ereignisse(void)
{
    double s = jetzt_ms / 1000.0;
    while (ev_i < ev_n && ev[ev_i].t <= s) {
        Ereignis *e = &ev[ev_i++];
        double dauer = atof(e->arg);
        if (!strcmp(e->art, "kreis")) kreis_bis = s + dauer;
        else if (!strcmp(e->art, "dreieck")) dreieck_bis = s + dauer;
        else if (!strcmp(e->art, "beide")) kreis_bis = dreieck_bis = s + dauer;
        else if (!strcmp(e->art, "laut")) laut_bis = s + dauer;
        else if (!strcmp(e->art, "licht")) licht = (uint16_t)atoi(e->arg);
        else if (!strcmp(e->art, "befehl")) befehl_einreihen(e->arg);
        else if (!strcmp(e->art, "ende")) {
            fprintf(aus, "{\"t\":%.0f,\"ende\":1}\n", jetzt_ms);
            fclose(aus);
            exit(0);
        }
    }
}

static long sekunde_jetzt(void) { return sek_basis + (long)(jetzt_ms / 1000.0); }

static void sekundentakt(void)
{
    long s = sekunde_jetzt();
    if (s == letzte_sek) return;
    letzte_sek = s;
    /* alarm 1: compare hour and minute, second 0 */
    if ((s % 60) == 0 && bcd(rtc[0x08] & 0x7F) == (s / 60) % 60 && bcd(rtc[0x09] & 0x3F) == (s / 3600) % 24)
        rtc[0x0F] |= 0x01;
}

void zw_rahmen(const uint8_t *bild)
{
    int i;
    rahmen_nr++;
    jetzt_ms = rahmen_nr * RAHMEN_MS;
    if (rahmen_nr == 1) drehbuch_laden();
    ereignisse();
    sekundentakt();
    if (erstes || memcmp(bild, letztes, SPALTEN)) {
        erstes = 0;
        memcpy(letztes, bild, SPALTEN);
        fprintf(aus, "{\"t\":%.0f,\"b\":\"", jetzt_ms);
        for (i = 0; i < SPALTEN; i++) fprintf(aus, "%02x", bild[i]);
        fprintf(aus, "\"}\n");
    }
}

uint8_t zw_rtc_lesen(uint8_t ab, uint8_t *ziel, uint8_t n)
{
    long s = sekunde_jetzt();
    rtc[0] = zubcd((uint8_t)(s % 60));
    rtc[1] = zubcd((uint8_t)((s / 60) % 60));
    rtc[2] = zubcd((uint8_t)((s / 3600) % 24));
    while (n--) *ziel++ = rtc[ab++ % 0x13];
    return 1;
}

static void ton_melden(const char *name)
{
    if (strcmp(name, ton_jetzt)) {
        ton_jetzt = name;
        if (aus) fprintf(aus, "{\"t\":%.0f,\"ton\":\"%s\"}\n", jetzt_ms, name);
    }
}

void zw_ton_name(const char *name) { ton_melden(name); }

static int led_alt = -1, hell_alt = -1;
void zw_led(uint8_t pegel)
{
    if (pegel != led_alt && aus) { led_alt = pegel; fprintf(aus, "{\"t\":%.0f,\"led\":%d}\n", jetzt_ms, pegel); }
}
void zw_hell(uint8_t stufe)
{
    if (stufe != hell_alt && aus) { hell_alt = stufe; fprintf(aus, "{\"t\":%.0f,\"hell\":%d}\n", jetzt_ms, stufe); }
}

uint8_t zw_rtc_schreiben(uint8_t ab, uint8_t wert)
{
    long s = sekunde_jetzt();
    if (ab <= 2) {
        long h = (s / 3600) % 24, m = (s / 60) % 60, sek = s % 60;
        if (ab == 0) sek = bcd(wert);
        if (ab == 1) m = bcd(wert);
        if (ab == 2) h = bcd(wert & 0x3F);
        sek_basis = h * 3600 + m * 60 + sek - (long)(jetzt_ms / 1000.0);
    } else if (ab < 0x13) {
        rtc[ab] = wert;
    }
    if (ab == 0x0E) {
        switch (wert & 0x1C) {
        case 0x04: case 0x0C: case 0x14: case 0x1C: ton_melden("aus"); break;
        case 0x08: ton_melden("1k"); break;
        case 0x10: ton_melden("4k"); break;
        case 0x18: ton_melden("8k"); break;
        default: ton_melden("1Hz"); break;
        }
    }
    return 1;
}

uint16_t zw_adc(uint8_t kanal)
{
    double s = jetzt_ms / 1000.0;
    if (kanal == 6) return licht;
    if (kanal == 7) return s < laut_bis ? 60 : 3;
    return 0;
}

uint8_t zw_mic(uint16_t schwelle)
{
    return (uint8_t)(zw_adc(7) > schwelle ? 3 : 0);
}

uint16_t zw_tastwert(uint8_t b)
{
    double s = jetzt_ms / 1000.0;
    if (b == 3) return (uint16_t)(288 + (s < kreis_bis ? 100 : 0));
    return (uint16_t)(256 + (s < dreieck_bis ? 100 : 0));
}

/* --- replacement for stc8.c --- */
static char zeile[256];
static int zeile_n;

void uart_init(void) {}
void ports_all_highz(void) {}
void uart_putc(uint8_t c)
{
    if (c == '\n' || zeile_n >= 250) {
        zeile[zeile_n] = 0;
        if (aus) {
            fprintf(aus, "{\"t\":%.0f,\"uart\":\"", jetzt_ms);
            for (int i = 0; i < zeile_n; i++)
                if (zeile[i] >= 32 && zeile[i] < 127 && zeile[i] != '"' && zeile[i] != '\\')
                    fputc(zeile[i], aus);
            fprintf(aus, "\"}\n");
        }
        zeile_n = 0;
    } else if (c != '\r') {
        zeile[zeile_n++] = (char)c;
    }
}
void uart_puts(const char *s) { while (*s) uart_putc((uint8_t)*s++); }
void uart_puthex8(uint8_t v) { const char *h = "0123456789ABCDEF"; uart_putc(h[v >> 4]); uart_putc(h[v & 15]); }
uint8_t uart_avail(void) { return rx_lesen_i != rx_schreib_i; }
uint8_t uart_getc(void)
{
    if (rx_lesen_i == rx_schreib_i) return '\n';
    return rx[rx_lesen_i++ & 1023];
}
