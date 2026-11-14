/*
 * Matrix clock - firmware with the features of the product description.
 *
 * Display 25 x 7, pinout see docs/PINOUT.md:
 *   rows    (source, high): P2.0 … P2.6, P2.0 at the top
 *   columns (sink,   low ): P3.0-P3.7, P0.0-P0.7, P1.0-P1.5, P4.0-P4.2,
 *                           P3.0 on the left
 *   touch P4.3 (circle) and P4.4 (triangle), light sensor P1.6 (ADC6),
 *   rear LEDs P2.7, DS3231 on P5.4/P5.5, buzzer on the clock's SQW output.
 *
 * The basic display is hh:mm:ss. That needs a 3x5 font: six digits in 5x7
 * would be 30 columns wide, the display has 25. With 3 columns per digit
 * the full time fits into 23 columns.
 *
 * Operation as on the original:
 *   triangle short  next screen (time / date / temperature)
 *   circle   short  rear LEDs on or off
 *   both     2 s    menu: ZEIT, DATUM, ALARM, TON, LED, ENDE
 *                   in it triangle = next, circle = select;
 *                   in the editor triangle = value up, circle = next field
 *
 * The alarm lives in the DS3231 alarm registers and therefore survives a
 * power failure - the controller remembers none of it.
 *
 * Brightness follows the light sensor. Every touch is acknowledged with a
 * short beep.
 *
 * While the display runs, P3.0/P3.1 are matrix columns and the UART is
 * dead. The firmware therefore blanks briefly at regular intervals and
 * listens - without this window it could no longer be replaced by a
 * software reset.
 */
#ifdef ZWILLING
#include "../zwilling/zwilling.h"
#endif
#include "stc8.h"
#include "schrift.h"      /* generated from host/schrift_art.py */

__sfr __at (0xBC) ADC_CONTR;
__sfr __at (0xBD) ADC_RES;
__sfr __at (0xBE) ADC_RESL;
__sfr __at (0xDE) ADCCFG;

#define SPALTEN   25
#define DS3231    0xD0

/* --------------------------------------------------------------- Font */

/* All tables are explicitly placed in program memory (__code).
 * Otherwise the compiler puts them into internal RAM - and the 8051 has
 * only 128 bytes of that, while the fonts alone need 190.
 *
 * Two font sizes, both column-wise, bit 0 is at the top.
 *
 * GROSS (large): 3 columns over all 7 rows, for hours and minutes.
 * KLEIN (small): 3 columns over 5 rows, bottom-aligned, for the seconds.
 *
 * Wider is impossible: the display has 25 columns. Six digits in the
 * 5x7 font would be 30 columns wide - that is why this font exists at all.
 */
static const __code uint8_t GROSS[2][10][3] = {
    {   /* Style A: square */
    { 0x7F, 0x41, 0x7F }, { 0x42, 0x7F, 0x40 }, { 0x79, 0x49, 0x4F },
    { 0x49, 0x49, 0x7F }, { 0x0F, 0x08, 0x7F }, { 0x4F, 0x49, 0x79 },
    { 0x7F, 0x49, 0x79 }, { 0x01, 0x01, 0x7F }, { 0x7F, 0x49, 0x7F },
    { 0x4F, 0x49, 0x7F }
    }, {   /* Style B: rounded */
    { 0x3E, 0x41, 0x3E }, { 0x42, 0x7F, 0x40 }, { 0x72, 0x49, 0x46 },
    { 0x22, 0x49, 0x36 }, { 0x0F, 0x08, 0x7F }, { 0x4F, 0x49, 0x31 },
    { 0x3E, 0x49, 0x32 }, { 0x01, 0x71, 0x0F }, { 0x36, 0x49, 0x36 },
    { 0x26, 0x49, 0x3E }
    }
};
/* 3x5, not yet shifted - drawing adds an offset of two rows so that
 * the small digits sit at the bottom. */
static const __code uint8_t KLEIN[10][3] = {
    { 0x1F, 0x11, 0x1F }, { 0x12, 0x1F, 0x10 }, { 0x1D, 0x15, 0x17 },
    { 0x11, 0x15, 0x1F }, { 0x07, 0x04, 0x1F }, { 0x17, 0x15, 0x1D },
    { 0x1F, 0x15, 0x1D }, { 0x01, 0x01, 0x1F }, { 0x1F, 0x15, 0x1F },
    { 0x17, 0x15, 0x1F }
};
/* Letters, also 3x7. Only those needed for the menu texts - complete
 * fonts cost space and bring nothing here. */
#define BU_A  0
#define BU_D  1
#define BU_E  2
#define BU_F  3
#define BU_H  4
#define BU_I  5
#define BU_L  6
#define BU_M  7
#define BU_N  8
#define BU_O  9
#define BU_R 10
#define BU_S 11
#define BU_T 12
#define BU_U 13
#define BU_Z 14
#define BU_C 15
#define BU_LEER 16
#define BU_P 17
#define BU_K 18
#define BU_G 19
#define BU_Q 20

static const __code uint8_t BUCHSTABE[21][3] = {
    { 0x7E, 0x09, 0x7E },   /* A */
    { 0x7F, 0x41, 0x3E },   /* D */
    { 0x7F, 0x49, 0x49 },   /* E */
    { 0x7F, 0x09, 0x09 },   /* F */
    { 0x7F, 0x08, 0x7F },   /* H */
    { 0x41, 0x7F, 0x41 },   /* I */
    { 0x7F, 0x40, 0x40 },   /* L */
    { 0x7F, 0x06, 0x7F },   /* M */
    { 0x7F, 0x1C, 0x7F },   /* N */
    { 0x3E, 0x41, 0x3E },   /* O */
    { 0x7F, 0x09, 0x76 },   /* R */
    { 0x4F, 0x49, 0x79 },   /* S */
    { 0x01, 0x7F, 0x01 },   /* T */
    { 0x3F, 0x40, 0x3F },   /* U */
    { 0x71, 0x49, 0x47 },   /* Z */
    { 0x3E, 0x41, 0x41 },   /* C */
    { 0x00, 0x00, 0x00 },   /* space */
    { 0x7F, 0x09, 0x0F },   /* P */
    { 0x7F, 0x14, 0x63 },   /* K */
    { 0x7F, 0x41, 0x79 },   /* G */
    { 0x3E, 0x51, 0x6E }    /* Q */
};

/* Six characters of 3 columns plus spacing fit into the 25 columns. */
static const __code uint8_t TEXTPOS[6] = { 0, 4, 8, 12, 16, 20 };

#define DOPPELPUNKT 0x14
#define PUNKT       0x40

/* THEME1 - layout of the 25 columns, specified and accepted by the user:
 *   1-3 H   4 blank   5-7 H   8 :   9 blank   10-12 m   13 blank   14-16 m
 *   17 :   18 blank   19-21 s   22 blank   23-25 s
 * All six digits at full height (3x7). The seconds used to be small
 * (3x5) and left two rows unused. */
#define G1 0
#define G2 4
#define TRENNER 7
#define G3 9
#define G4 13
#define TRENNER2 16
#define K1 18
#define K2 22

/* --------------------------------------------------------------- Ports */

static const __code uint8_t SPALTE[SPALTEN] = {
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x40, 0x41, 0x42
};

/* Timer and interrupt registers. IE is not in stc8.h. */
__sfr __at (0x88) TCON;
__sfr __at (0x89) TMOD;
__sfr __at (0x8A) TL0;
__sfr __at (0x8B) TL1;
__sfr __at (0x8C) TH0;
__sfr __at (0x8D) TH1;
__sbit __at (0x8C) TR0;
__sbit __at (0x8D) TF0;
__sbit __at (0x8E) TR1;
__sbit __at (0x8F) TF1;
__sbit __at (0xA9) ET0;
__sbit __at (0xAF) EA;
__sbit __at (0xAC) ES;
__sfr __at (0xB8) IP;
__sfr __at (0xBA) P_SW2;       /* bit 7 EAXFR: extended registers from 0xFE00 */


/* ------------------------------------------------- Display in the interrupt
 *
 * Scanning runs in timer 0, no longer in the main loop.
 *
 * It used to run there, and every other task stopped it: key measurement
 * 2.4 ms every 20 frames, reading the clock 3.0 ms every 100 (measured).
 * During that time the display stood still, i.e. dark - about 10 times per
 * second a gap of more than half a frame, right in the most sensitive
 * flicker range. The rear LEDs were hit twice: they only got a short pulse
 * at the end of each frame, and that was lost too.
 *
 * Now the interrupt does two things per column: switch on and, after the
 * on-time, switch off. The main loop draws into `bild`, rahmen() copies it
 * atomically to `scan` and waits for the end of a frame.
 * Brightness is the on-time; the period stays the same.
 *
 * The rear LEDs (P2.7) are lit continuously in every fourth column, i.e.
 * with a 25 % duty cycle spread over the whole frame. P2.7 is not a matrix
 * row and may therefore also be lit during a column. */
#define SLOT_TICKS 3450u        /* 187 us per column at 18.432 MHz in 1T mode */

static __xdata uint8_t bild[SPALTEN];   /* drawn by the main program */
static __xdata uint8_t scan[SPALTEN];   /* copy, read by the interrupt */
/* Target of the drawing functions: normally bild, for animations the
 * invisible target image (anzeige.c). */
static __xdata uint8_t *zb;
static volatile uint8_t helligkeit = 6; /* 1 to 8 */
static volatile uint8_t led_pegel = 0;  /* rear LEDs: 0..64 (64 = always on) */
static uint8_t led_summe, led_merk_rl;  /* interrupt only: distribution */
/* Display settings (stored in EEPROM, see einst_speichern) */
static uint8_t wechsel = 2, ziffer_eff = 0, dpunkt = 5, info_int = 2;
static uint8_t schrift = 1;     /* 1..3: theme 2 style 1-3; 3x7: 2 = rounded, otherwise square */
static uint8_t led_eff = 0, led_merk = 1;  /* rear LEDs: 0 off, 1 constant, 2 sound, 3 breathing */
static uint8_t hell_fest;       /* 0: from light sensor, 1..8: fixed level */
static uint8_t mikro = 5;       /* microphone threshold in tens (light organ, rear LEDs "Ton") */
#define MIKRO_MIN 2             /* setting "Mikro": 20..90 */
#define MIKRO_MAX 9
static volatile uint8_t uart_frei = 0;  /* 1: P3.0 dark, 2: P3.0 and P3.1, 3: P3.1 */

static volatile uint8_t aktiv = 0;
static volatile uint8_t frames = 0;
static volatile uint16_t frames_gesamt = 0;   /* for checking, wraps around */
static volatile uint8_t isr_c = 0;
static volatile uint8_t isr_phase = 0;
static volatile uint16_t t_an = 0, t_aus = 0;   /* counter start values */
/* Separate on-time for up to two columns (fading the colon): real PWM
 * in every frame instead of on/off over several frames - that gave
 * 45 Hz and flickered visibly. 0xFF = no column. */
static volatile uint16_t t_an_dp = 0, t_aus_dp = 0;
static volatile uint8_t dp_s1 = 0xFF, dp_s2 = 0xFF;
/* For measurements with EA = 0: counter of timer interrupts and whether the
 * interval just started is long (at least 49 us). Whoever disables
 * interrupts briefly in such an interval shifts no switching time of the
 * display. */
#define LANG_TAKTE 900u
static volatile uint8_t isr_takt = 0, isr_lang = 0, an_lang = 0, aus_lang = 0;
static uint8_t lat0 = 0xFF, lat1 = 0xFF, lat3 = 0xFF, lat4 = 0xFF;

/* ---------------------------------------------- Listening while displaying
 *
 * Column 1 is on RxD (P3.0), and its LEDs sit between the rows and this
 * pin. In every on-phase the lit rows are high and feed current into the
 * receive line through these LEDs; the sender can then no longer hold a
 * clean low. Measured: display dark 100 %, one to three rows about 78 %,
 * all seven practically nothing.
 * That is also why all earlier attempts to handle listening via column 1
 * alone failed - the interference comes from every column.
 *
 * The firmware therefore listens only in pauses in which all rows are low:
 * after every PAUSE_ALLE-th frame, PAUSE_SLOTS dark time slots. The pause
 * hits all columns equally, the display gets a few percent darker, but
 * nothing flickers. The sender first sends 0xFF without gaps, for at least
 * one pause interval. If two 0xFF arrive within one pause, the matrix stays
 * dark (halten), the firmware acknowledges with '!' and receives the line
 * undisturbed; afterwards the display continues. Bytes outside the pauses
 * are garbage and are discarded.
 *
 * 0xFF, because a gapless stream of it has only one falling edge per
 * character, the start bit: the receiver locks back in correctly right
 * after a disturbance. 0xFF never occurs in UTF-8 text. */
#define PAUSE_TICKS  1475u      /* listening pause: 80 us, a good 7 characters at 921600 baud */
#define PAUSE_SPALTEN 5         /* a pause after every 5th column: 5 per frame,
                                 * at most 1.1 ms apart, about 7 % brightness */
#define SYNC_ZAHL    3          /* this many 0xFF in ONE pause; a glitch at the
                                 * start of the pause gives at most one */
#define HALTE_LUECKE 2          /* dark frames (5 ms each) without characters until abort */
#define HALTE_AUSF   10         /* after the end of line: time for the main loop */
#define HALTE_MAX    20         /* upper limit, about 100 ms, whatever arrives */

volatile __xdata uint8_t rx_buf[64];    /* read by stc8.c (UART_RX_IRQ), halted state only */
volatile uint8_t rx_w, rx_r;
volatile uint8_t tx_puffern, tx_len;    /* reply buffer, see stc8.c */
__xdata uint8_t tx_buf[128];
static volatile uint16_t rx_n, rx_sync, rx_zeilen, rx_fehler, dunkel_slots;
static volatile uint8_t pause_anst = 0, pause_aktiv = 0, halten = 0, halte_frist = 0;
static volatile uint8_t zeile_da = 0, bild_neu = 0, pause_z = 0;
static volatile uint8_t halte_dauer = 0;
static volatile uint8_t ausfuehrung = 0;   /* set only by the main loop */
static volatile uint8_t nacht_dunkel = 0;  /* night mode: display off */
static uint8_t orgel_aktiv;                /* light organ draws by itself */
static volatile uint8_t ff_folge = 0, leer_c = 0, slot_dunkel = 0;

/* Live command line, written by the UART interrupt. 254 command bytes plus
 * the check character fit; uint8_t lengths do not allow more.
 *
 * It used to go through a 64-byte ring buffer that the main loop only
 * emptied once per frame - while a line came in, the main loop waited for the
 * end of the frame. Lines longer than about 61 bytes therefore only got
 * through if a frame end happened to fall into them; otherwise the rest, often
 * including the '\n', was lost without a trace (found with an ESP8285 on
 * 2026-11-13). A bigger ring with 16-bit pointers needed locks and still lost
 * bytes now and then; writing the line directly in the interrupt needs none. */
static __xdata uint8_t zeile[255];
static volatile uint8_t zeile_len, zeile_auf, zeile_fertig, zeile_lang;
static uint8_t zeile_pos, live;

void uart_isr(void) __interrupt(4) __using(2)
{
    uint8_t c;

    if (TI)
        TI = 0;                         /* only the acknowledgement; uart_putc disables ES */
    if (!RI)
        return;
    RI = 0;
    c = SBUF;
    rx_n++;
    if (aktiv && !halten) {
        if (!pause_aktiv || c != 0xFF) {
            ff_folge = 0;
            return;
        }
        if (++ff_folge < SYNC_ZAHL)
            return;
        ff_folge = 0;
        halten = 1;
        halte_frist = HALTE_LUECKE;
        halte_dauer = 0;
        rx_sync++;
        SBUF = '!';                     /* TxD is push-pull during the pause */
        if (!zeile_fertig) {            /* otherwise the previous line still runs */
            zeile_auf = 1;
            zeile_len = 0;
            zeile_lang = 0;
        }
        return;
    }
    if (aktiv) {
        /* Live operation: the line goes straight into zeile[], 8-bit index,
         * no shared pointers. While zeile_fertig is set the main loop owns
         * the line, and nothing is written. */
        if (c == 0xFF)
            return;                     /* remaining sync bytes */
        if (c == '\n' || c == '\r') {
            halte_frist = HALTE_AUSF;
            zeile_da = 1;               /* main loop does not wait for the end of the frame */
            if (zeile_auf) {
                zeile_auf = 0;
                zeile_fertig = 1;
            }
            return;
        }
        halte_frist = HALTE_LUECKE;
        if (!zeile_auf)
            return;
        if (zeile_len < sizeof zeile)
            zeile[zeile_len++] = c;
        else
            zeile_lang = 1;
        return;
    }
    if ((uint8_t)(rx_w - rx_r) < 64) {  /* halted state: ring buffer, read directly */
        rx_buf[rx_w & 63] = c;
        rx_w++;
    }
}


/* On-time per level in timer ticks (time slot 3450). Exponential, about a
 * factor of 1.5 to 2 per level: the eye perceives brightness
 * logarithmically. Previously linear 40 to 87 % - the darkest level was far
 * too bright at night and the difference hardly visible. At the top about
 * 300 ticks (16 us) remain for the interrupt that switches the column off
 * again. */
static const __code uint16_t AN_TAKTE[8] = { 69, 207, 345, 800, 1200, 1750, 2450, 3150 };
/* 2 %, 6 %, 10 % (chosen by the user), 23 %, 35 %, 51 %, 71 %, 91 % */

static void helligkeit_uebernehmen(void)
{
    uint16_t an = AN_TAKTE[(uint8_t)(helligkeit - 1) & 7];
    uint16_t aus = (uint16_t)(SLOT_TICKS - an);

    EA = 0;
    t_an = (uint16_t)(65536UL - an);
    t_aus = (uint16_t)(65536UL - aus);
    an_lang = (uint8_t)(an >= LANG_TAKTE);
    aus_lang = (uint8_t)(aus >= LANG_TAKTE);
    EA = 1;
#ifdef ZWILLING
    zw_hell(helligkeit);
#endif
}

/* Colon columns s1/s2 with stufe/8 of the normal on-time;
 * s1 = 0xFF switches the special PWM off. */
static void dp_dimmen(uint8_t s1, uint8_t s2, uint8_t stufe)
{
    uint16_t an = (uint16_t)((uint32_t)AN_TAKTE[(uint8_t)(helligkeit - 1) & 7] * stufe / 8u);

    if (an < 20)
        an = 20;                            /* the interrupt needs some distance */
    EA = 0;
    t_an_dp = (uint16_t)(65536UL - an);
    t_aus_dp = (uint16_t)(65536UL - (SLOT_TICKS - an));
    dp_s1 = s1;
    dp_s2 = s2;
    EA = 1;
}

void timer0_isr(void) __interrupt(1) __using(1)
{
    uint8_t pb, p, m, rl, aus, i;

    if (isr_phase == 0 && pause_anst) {
        /* Listening pause before the frame: all rows low, RxD quasi-bidirectional
         * with pull-up (without a sender it stays high), TxD push-pull high
         * for the acknowledgement. Only 0xFF arriving now count as sync. */
        pause_anst = 0;
        pause_aktiv = 1;
        TH0 = (uint8_t)((65536u - PAUSE_TICKS) >> 8);
        TL0 = (uint8_t)(65536u - PAUSE_TICKS);
        isr_lang = (uint8_t)(PAUSE_TICKS >= LANG_TAKTE);
        isr_takt++;
        P2 = 0x00;
        lat3 |= 0x03;
        P3 = lat3;
        P3M1 &= (uint8_t)~0x03;
        P3M0 &= (uint8_t)~0x01;
        P3M0 |= 0x02;
        return;
    }
    if (pause_aktiv) {
        pause_aktiv = 0;
        ff_folge = 0;                   /* sync only counts within a pause */
    }

    pb = SPALTE[isr_c];
    p = (uint8_t)(pb >> 4);
    m = (uint8_t)(1u << (pb & 0x0F));
    /* Rear LEDs: level/64 of the time slots, evenly distributed (sum with
     * overflow). Previously level/8 in a fixed pattern over 8 columns - that
     * looked stepped and flickering while breathing. */
    if (isr_phase == 0) {
        led_summe = (uint8_t)(led_summe + led_pegel);
        led_merk_rl = (uint8_t)(led_summe >= 64);
        if (led_merk_rl)
            led_summe = (uint8_t)(led_summe - 64);
    }
    rl = led_merk_rl;
    /* In update mode P3.0/P3.1 stay dark (uart_frei). */
    aus = (uint8_t)(p == 3 && ((m == 0x01 && (uart_frei == 1 || uart_frei == 2))
                               || (m == 0x02 && (uart_frei == 2 || uart_frei == 3))));

    if (isr_phase == 0) {
        /* Holding or executing: whole time slot dark, rows low. Latch it for
         * both phases, otherwise a column would stay switched on if holding
         * starts in the middle of a time slot. */
        slot_dunkel = (uint8_t)(halten || ausfuehrung || nacht_dunkel);
        if (isr_c == dp_s1 || isr_c == dp_s2) {
            TH0 = (uint8_t)(t_an_dp >> 8);
            TL0 = (uint8_t)t_an_dp;
        } else {
            TH0 = (uint8_t)(t_an >> 8);
            TL0 = (uint8_t)t_an;
        }
        isr_lang = an_lang;
        isr_takt++;
        if (slot_dunkel) {
            P2 = 0x00;
            dunkel_slots++;             /* measurement: dark time caused by commands */
        } else if (!aus) {
            P2 = (uint8_t)((rl ? 0x80 : 0x00) | (scan[isr_c] & 0x7F));
            switch (p) {                    /* latch low first, then push-pull */
            case 0: lat0 &= (uint8_t)~m; P0 = lat0; P0M1 &= (uint8_t)~m; P0M0 |= m; break;
            case 1: lat1 &= (uint8_t)~m; P1 = lat1; P1M1 &= (uint8_t)~m; P1M0 |= m; break;
            case 3: lat3 &= (uint8_t)~m; P3 = lat3; P3M1 &= (uint8_t)~m; P3M0 |= m; break;
            default: lat4 &= (uint8_t)~m; P4 = lat4; P4M1 &= (uint8_t)~m; P4M0 |= m; break;
            }
        } else {
            P2 = (uint8_t)(rl ? 0x80 : 0x00);
        }
        isr_phase = 1;
        return;
    }

    if (isr_c == dp_s1 || isr_c == dp_s2) {
        TH0 = (uint8_t)(t_aus_dp >> 8);
        TL0 = (uint8_t)t_aus_dp;
    } else {
        TH0 = (uint8_t)(t_aus >> 8);
        TL0 = (uint8_t)t_aus;
    }
    isr_lang = aus_lang;
    isr_takt++;
    if (!aus && !slot_dunkel) {
        switch (p) {                        /* high-impedance first, then latch high */
        case 0: P0M1 |= m; P0M0 &= (uint8_t)~m; lat0 |= m; P0 = lat0; break;
        case 1: P1M1 |= m; P1M0 &= (uint8_t)~m; lat1 |= m; P1 = lat1; break;
        case 3: P3M1 |= m; P3M0 &= (uint8_t)~m; lat3 |= m; P3 = lat3; break;
        default: P4M1 |= m; P4M0 &= (uint8_t)~m; lat4 |= m; P4 = lat4; break;
        }
    }
    P2 = (uint8_t)(slot_dunkel ? 0x00 : (rl ? 0x80 : 0x00));
    isr_phase = 0;
    if (++pause_z >= PAUSE_SPALTEN) {       /* listening pauses in the dark too */
        pause_z = 0;
        pause_anst = 1;
    }
    if (slot_dunkel) {
        if (++leer_c < SPALTEN)
            return;
        leer_c = 0;                         /* frames count in the dark too */
        if (halten && (--halte_frist == 0 || ++halte_dauer >= HALTE_MAX))
            halten = 0;
    } else {
        if (++isr_c < SPALTEN)
            return;
        isr_c = 0;
    }
    /* End of frame: take over the new image. Here in the interrupt instead of
     * in the main loop with EA = 0 - the copy takes about 20 us, and at
     * 921600 baud only 11 us remain between two characters. The UART
     * interrupt has priority and interrupts the copy. */
    if (bild_neu) {
        for (i = 0; i < SPALTEN; i++)
            scan[i] = bild[i];
        bild_neu = 0;
    }
    frames++;
    frames_gesamt++;
}

static void anzeige_an(void)
{
    if (aktiv)
        return;
    lat0 = 0xFF; lat1 = 0xFF; lat3 = 0xFF; lat4 = 0xFF;
    P0 = 0xFF; P1 = 0xFF; P3 = 0xFF; P4 = 0xFF;
    P2M1 = 0x00;                    /* rows and rear LED push-pull */
    P2M0 = 0xFF;
    P2 = 0x00;
    isr_c = 0;
    isr_phase = 0;
    pause_anst = 0;
    pause_aktiv = 0;
    pause_z = 0;
    leer_c = 0;
    helligkeit_uebernehmen();
    AUXR |= 0x80;                   /* timer 0 in 1T mode */
    TMOD = (uint8_t)((TMOD & 0xF0) | 0x01);   /* mode 1: 16 bit */
    TH0 = 0xFF;
    TL0 = 0xF0;                     /* overflow right away */
    TF0 = 0;
    TR0 = 1;
    ET0 = 1;
    EA = 1;
    aktiv = 1;
}

/* Display off and all ports high-impedance - for the UART and for the
 * hold state. ports_all_highz() alone is no longer enough: the interrupt
 * would drive the columns again at the next time slot. */
static void anzeige_aus(void)
{
    ET0 = 0;
    TR0 = 0;
    aktiv = 0;
    ports_all_highz();
}

/* Waits for the end of a frame and takes over the image. The copy is
 * atomic so that a half-drawn image never appears. */
static void dp_dimmen(uint8_t s1, uint8_t s2, uint8_t stufe);
static void led_tick(void);

static void rahmen(void)
{
    led_tick();                     /* rear-LED effects in menu and alarm too */
    /* rahmen() is used only by the menu, editors, alarm and puzzle - the
     * colon's special PWM does not apply there. It had been calculated
     * with the old brightness and made column 13 (the second L of HELL)
     * flash brightly in the menu. */
    if (dp_s1 != 0xFF)
        dp_dimmen(0xFF, 0xFF, 8);
#ifdef ZWILLING
    zw_rahmen(bild); frames++; frames_gesamt++; return;
#endif
    uint8_t f;

    anzeige_an();
    bild_neu = 1;
    f = frames;
    while (f == frames)
        ;
}

/* Like rahmen(), but returns as soon as a command line is complete -
 * while holding, the display is dark and every millisecond counts.
 * Main loop only. */
static void rahmen_oder_zeile(void)
{
#ifdef ZWILLING
    zw_rahmen(bild); frames++; frames_gesamt++; return;
#endif
    uint8_t f;

    anzeige_an();
    bild_neu = 1;
    f = frames;
    while (f == frames && !zeile_da)
        ;
}

/* ----------------------------------------------------------------- I2C */

static uint8_t p5 = 0xFF;
#define SCL_M 0x10
#define SDA_M 0x20

static uint8_t i2c_schnell;     /* 1: without wait loop, about 400 kHz (for 520 Hz) */

static void i2c_pause(void)
{
    volatile uint8_t w;

    if (i2c_schnell)
        return;
    for (w = 0; w < 10; w++)
        ;
}

static void i2c_init(void)
{
    p5 |= (uint8_t)(SCL_M | SDA_M);
    P5 = p5;
    P5M1 |= (uint8_t)(SCL_M | SDA_M);
    P5M0 |= (uint8_t)(SCL_M | SDA_M);
}

static void scl(uint8_t h)
{
    if (h) p5 |= SCL_M; else p5 = (uint8_t)(p5 & ~SCL_M);
    P5 = p5; i2c_pause();
}

static void sda(uint8_t h)
{
    if (h) p5 |= SDA_M; else p5 = (uint8_t)(p5 & ~SDA_M);
    P5 = p5; i2c_pause();
}

static uint8_t sda_pegel(void) { return (uint8_t)((P5 & SDA_M) ? 1 : 0); }
static void i2c_start(void) { sda(1); scl(1); sda(0); scl(0); }
static void i2c_stop(void)  { sda(0); scl(1); sda(1); }

static uint8_t i2c_schreiben(uint8_t v)
{
    uint8_t i, q;

    for (i = 0; i < 8; i++) {
        scl(0); sda((uint8_t)(v & 0x80)); scl(1);
        v = (uint8_t)(v << 1);
    }
    scl(0); sda(1); scl(1);
    q = (uint8_t)(sda_pegel() == 0);
    scl(0);
    return q;
}

static uint8_t i2c_lesen(uint8_t ack)
{
    uint8_t i, v = 0;

    sda(1);
    for (i = 0; i < 8; i++) {
        scl(0); scl(1);
        v = (uint8_t)((v << 1) | sda_pegel());
    }
    scl(0); sda((uint8_t)(ack ? 0 : 1)); scl(1); scl(0); sda(1);
    return v;
}

static uint8_t rtc_lesen(uint8_t ab, uint8_t *ziel, uint8_t n)
{
#ifdef ZWILLING
    return zw_rtc_lesen(ab, ziel, n);
#endif
    uint8_t i;

    i2c_init();
    i2c_start();
    if (!i2c_schreiben(DS3231))     { i2c_stop(); return 0; }
    if (!i2c_schreiben(ab))         { i2c_stop(); return 0; }
    i2c_start();
    if (!i2c_schreiben(DS3231 | 1)) { i2c_stop(); return 0; }
    for (i = 0; i < n; i++)
        ziel[i] = i2c_lesen((uint8_t)(i + 1 < n));
    i2c_stop();
    return 1;
}

static uint8_t rtc_schreiben(uint8_t ab, uint8_t wert)
{
#ifdef ZWILLING
    return zw_rtc_schreiben(ab, wert);
#endif
    i2c_init();
    i2c_start();
    if (!i2c_schreiben(DS3231)) { i2c_stop(); return 0; }
    if (!i2c_schreiben(ab))     { i2c_stop(); return 0; }
    if (!i2c_schreiben(wert))   { i2c_stop(); return 0; }
    i2c_stop();
    return 1;
}

/* The buzzer hangs off the INT/SQW output of the DS3231 via Q1. Tone =
 * square wave on this pin, selected in control register 0x0E: INTCN = 0 and
 * RS2/RS1 select the frequency. Off = INTCN = 1.
 *
 * The alarm interrupt (A1IE) always stays off. With A1IE = 1 the DS3231
 * pulled the pin low at the alarm time until the firmware noticed - that
 * was the click at exactly 02:08. The DS3231 sets the A1F flag even
 * without A1IE, and the firmware polls that. The alarm enable is therefore
 * no longer in A1IE but in bit 0 of register 0x0A (see alarm_schreiben). */
#define TON_AUS  0
#define TON_1K   1              /* 1.024 kHz */
#define TON_4K   2              /* 4.096 kHz */
#define TON_8K   3              /* 8.192 kHz */
static const __code uint8_t TON_REG[4] = { 0x04, 0x08, 0x10, 0x18 };
static uint8_t alarm_an;
#define MELODIEZAHL 5           /* four tables, the fifth is t3_520() */
static uint8_t melodie;         /* 0..4, stored in 0x0C */

static void ton(uint8_t f)
{
    rtc_schreiben(0x0E, TON_REG[f & 3]);
}

static void buzzer(uint8_t an)
{
    ton(an ? TON_4K : TON_AUS);
}

static uint8_t melodie_spielen(uint8_t nr, uint16_t max_rahmen, uint8_t abbruch);

/* ----------------------------------------------------------------- ADC */

static void adc_init(void)
{
    ADCCFG = 0x2F;
    ADC_CONTR = 0x80;
}

static uint16_t adc_lesen(uint8_t kanal)
{
#ifdef ZWILLING
    return zw_adc(kanal);
#endif
    volatile uint8_t w;

    ADC_CONTR = (uint8_t)(0x80 | (kanal & 0x0F));
    for (w = 0; w < 20; w++)
        ;
    ADC_CONTR |= 0x40;
    while (!(ADC_CONTR & 0x20))
        ;
    ADC_CONTR = (uint8_t)(ADC_CONTR & ~0x20);
    return (uint16_t)(((uint16_t)ADC_RES << 8) | ADC_RESL);
}

/* Dark gives about 4000, light less - the display follows inversely. */
/* Light sensor: dark = high value (covered 4095), room light at night
 * about 3670 (measured). The curve used to be linear in steps of 512 -
 * everything above 3584 was level 1, so nothing changed in the room. Now
 * logarithmic in the distance to 4095: each level twice as much light. */
static uint16_t licht_wert;     /* last measured: 4095 - ADC, large = bright */
static uint8_t hell_gesetzt;   /* once without hysteresis after start-up */

static const __code uint16_t HELL_SCHWELLE[7] = { 30, 60, 120, 240, 480, 960, 1920 };

static void helligkeit_regeln(void)
{
    uint16_t licht = (uint16_t)(4095u - adc_lesen(6));
    uint8_t stufe = 1;

    licht_wert = licht;
    if (hell_fest) {
        helligkeit = hell_fest;
        helligkeit_uebernehmen();
        return;
    }
    /* With a margin (12.5 %) around every threshold: the light value
     * fluctuates by a good 5 % (also from the clock's own display light,
     * e.g. the pulsing colon). If it sat exactly on a threshold, the level
     * jumped back and forth and the whole display got visibly brighter and
     * darker. */
    while (stufe < 8 && licht >= HELL_SCHWELLE[stufe - 1])
        stufe++;
    if (!hell_gesetzt) {                        /* first measurement: set level directly */
        hell_gesetzt = 1;
        helligkeit = stufe;
    } else if (helligkeit < 8 && licht >= HELL_SCHWELLE[helligkeit - 1] + (HELL_SCHWELLE[helligkeit - 1] >> 3))
        helligkeit++;                           /* one level per check */
    else if (helligkeit > 1 && licht + (HELL_SCHWELLE[helligkeit - 2] >> 3) < HELL_SCHWELLE[helligkeit - 2])
        helligkeit--;
    helligkeit_uebernehmen();
}

/* ------------------------------------------------------------ Night mode
 *
 * If it is enabled and the room has been dark for 10 s, the display goes
 * off. Clapping, a loud noise or a touch switch it on for 15 s; if the
 * light comes on, it continues normally.
 *
 * Microphone P1.7 (ADC 7), measured with the display dark: quiet at most
 * 7-9, once 14; clapping 47-59 with 2 to 19 values above 20 per second.
 * With the display lit, the LEDs couple in up to 20 - so listening happens
 * only in the dark state, and it is quiet there. Wake-up: at least two
 * values above MIC_SCHWELLE within 10 frames (about 56 ms). */
#define MIC_SCHWELLE   20
#define MIC_TREFFER    2
#define NACHT_DUNKEL   100      /* light value below = dark. Measured: room light
                                 * 450, covered 5 to 60 - the clock's own display
                                 * shines onto the sensor through the finger */
#define NACHT_HELL     200      /* above = light on (margin against toggling) */
#define NACHT_WARTEN   20       /* 20 checks of 0.5 s = 10 s dark */
#define WACH_RAHMEN    2700     /* about 15 s */

static uint8_t nacht_modus;     /* setting, stored in 0x0D */
static uint16_t wach_rahmen;    /* > 0: display on although night */
static uint8_t dunkel_zaehler, laut, laut_fenster;
static uint8_t aus_manuell;
static uint8_t weck_mic, weck_touch;   /* measurement: wake-up reasons */
static uint16_t mic_spitze;             /* highest microphone value in the dark */     /* Q2: off until the next wake-up, whatever the light */

static void wecken(void)
{
    nacht_dunkel = 0;
    aus_manuell = 0;
    laut = 0;
    if (nacht_modus)
        wach_rahmen = WACH_RAHMEN;
}

/* Wait one frame like rahmen_oder_zeile() and sample the microphone
 * meanwhile. Returns: number of loud values. */
static uint8_t klatsch_rahmen(uint16_t schwelle)
{
#ifdef ZWILLING
    zw_rahmen(bild); frames++; frames_gesamt++; return zw_mic(schwelle);
#endif
    uint8_t f, n = 0;

    anzeige_an();
    bild_neu = 1;
    f = frames;
    uint16_t v;

    while (f == frames && !zeile_da) {
        v = adc_lesen(7);
        if (v > mic_spitze)
            mic_spitze = v;
        if (v > schwelle && n < 255)
            n++;
    }
    return n;
}

/* Every 0.5 s after measuring the brightness: switch on or off. */
static void nacht_pruefen(void)
{
    if (orgel_aktiv) {          /* light organ: display stays on */
        nacht_dunkel = 0;
        return;
    }
    if (aus_manuell)
        return;
    if (!nacht_modus) {
        nacht_dunkel = 0;
        wach_rahmen = 0;
        return;
    }
    if (wach_rahmen)            /* do not evaluate during the 15 s: the clock's own */
        return;                 /* display shines onto the sensor */
    if (nacht_dunkel) {
        if (licht_wert >= NACHT_HELL)
            nacht_dunkel = 0;
        dunkel_zaehler = 0;
    } else if (licht_wert < NACHT_DUNKEL) {
        if (++dunkel_zaehler >= NACHT_WARTEN) {
            dunkel_zaehler = 0;
            nacht_dunkel = 1;
        }
    } else {
        dunkel_zaehler = 0;
    }
}



/* --------------------------------------------------------------- Touch */

/* Drive the pin low, release it, count until the pull-up pulls it high -
 * and sum that up 32 times. The charge time is one to two microseconds,
 * a single measurement is far too coarse for that. */
/* Waits until the display interrupt has just run and the next interval is
 * long. Immediately when the display is off. */
static void ruhefenster(void)
{
#ifdef ZWILLING
    return;
#endif
    uint8_t t;

    if (!aktiv)
        return;
    do {
        t = isr_takt;
        while (t == isr_takt)
            ;
    } while (!isr_lang);
}

static uint16_t tastwert(uint8_t b)
{
#ifdef ZWILLING
    return zw_tastwert(b);
#endif
    uint16_t summe = 0, r;
    uint8_t n, m = (uint8_t)(1u << b);
    volatile uint8_t w;

    for (r = 0; r < 32; r++) {
        /* Interrupts are disabled only briefly (about 15 us) and only in a
         * long interval of the display timer. Previously it was 70 us right
         * after the start of the frame: the switch-off interrupts of the
         * first ten columns came late, which at 4 % on-time doubled their
         * brightness - the left two modules flickered about 9 times per
         * second. Disabling is needed at all because the interrupt writes
         * the same port registers and would falsify the count. */
        ruhefenster();
        EA = 0;
        P4M1 &= (uint8_t)~m; P4M0 |= m;
        P4 = (uint8_t)(P4 & ~m);
        for (w = 0; w < 2; w++)         /* discharge; the pad needs nanoseconds */
            ;
        P4M1 |= m; P4M0 &= (uint8_t)~m;
        n = 0;
        while (n < 64 && !(P4 & m))
            n++;
        EA = 1;
        summe += n;
    }
    return summe;
}

static uint16_t ruhe_kreis, ruhe_dreieck;
static uint8_t war_kreis, war_dreieck;

static void touch_einmessen(void)
{
    ruhe_kreis = tastwert(3);
    ruhe_dreieck = tastwert(4);
}

#define TASTSCHWELLE 40

static void touch_lesen(uint8_t *kreis, uint8_t *dreieck)
{
    uint16_t k = tastwert(3), d = tastwert(4);

    *kreis = (uint8_t)(k > ruhe_kreis + TASTSCHWELLE);
    *dreieck = (uint8_t)(d > ruhe_dreieck + TASTSCHWELLE);

    /* Slowly track the idle value while nothing is pressed -
     * temperature and humidity otherwise shift it away over hours. */
    if (!*kreis && k < ruhe_kreis + TASTSCHWELLE / 2)
        ruhe_kreis = (uint16_t)((ruhe_kreis * 15 + k) >> 4);
    if (!*dreieck && d < ruhe_dreieck + TASTSCHWELLE / 2)
        ruhe_dreieck = (uint16_t)((ruhe_dreieck * 15 + d) >> 4);
}

/* ------------------------------------------------------------- Content */

static uint8_t bcd(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t zubcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static void leeren(void)
{
    uint8_t i;

    for (i = 0; i < SPALTEN; i++)
        zb[i] = 0;
}

static void gross(uint8_t ab, uint8_t z)
{
    uint8_t st = (uint8_t)(schrift == 2);   /* 3x7 exists as A and B */

    zb[ab] = GROSS[st][z][0];
    zb[ab + 1] = GROSS[st][z][1];
    zb[ab + 2] = GROSS[st][z][2];
}

static void klein(uint8_t ab, uint8_t z)
{
    zb[ab] = (uint8_t)(KLEIN[z][0] << 2);
    zb[ab + 1] = (uint8_t)(KLEIN[z][1] << 2);
    zb[ab + 2] = (uint8_t)(KLEIN[z][2] << 2);
}

/* hh : mm : ss, all in the large font. klein() is kept for later. */
static void anzeige_t1(uint8_t a, uint8_t b, uint8_t c, uint8_t trenner)
{
    leeren();
    gross(G1, (uint8_t)(a / 10));
    gross(G2, (uint8_t)(a % 10));
    zb[TRENNER] = trenner;
    gross(G3, (uint8_t)(b / 10));
    gross(G4, (uint8_t)(b % 10));
    zb[TRENNER2] = trenner;
    gross(K1, (uint8_t)(c / 10));
    gross(K2, (uint8_t)(c % 10));
}

/* ---- Theme2: hh:mm in 5x7, without seconds, as on the product photo ----
 * 5+1+5+1+1+1+5+1+5 = 25 columns.
 *
 * READ OFF two product photos are 1, 2, 3, 5 and 6 as well as the colon
 * (two dots of two LEDs height each). 3, 5 and 6 appear in both photos and
 * are identical there. The 2 has a full middle bar (5 LEDs) and a bottom
 * bar starting at column 2.
 * Confirmed as fitting by the user: 0, 4, 7, 8, 9.
 * BUILT, not read off, are 0, 4, 7, 8, 9 and the C - they do not appear in
 * the photo and are added in the same style: verticals two LEDs wide,
 * horizontals one LED high, curves as on the 6. The 9 is the 6 rotated by
 * 180 degrees. How the original really draws them is unknown. */
static const __code uint8_t ZIFFER5[3][10][5] = {
    {   /* Style 1: after the product photo */
    { 0x3E, 0x7F, 0x41, 0x7F, 0x3E }, { 0x00, 0x42, 0x7F, 0x7F, 0x40 },
    { 0x39, 0x79, 0x49, 0x4F, 0x4E }, { 0x49, 0x49, 0x49, 0x7F, 0x36 },
    { 0x0F, 0x0F, 0x08, 0x7F, 0x7F }, { 0x4F, 0x4F, 0x49, 0x79, 0x31 },
    { 0x3E, 0x7F, 0x49, 0x79, 0x32 }, { 0x01, 0x01, 0x79, 0x7F, 0x07 },
    { 0x36, 0x7F, 0x49, 0x7F, 0x36 }, { 0x26, 0x4F, 0x49, 0x7F, 0x3E }
    }, {   /* Style 2: classic thin */
    { 0x3E, 0x41, 0x41, 0x41, 0x3E }, { 0x00, 0x42, 0x7F, 0x40, 0x00 },
    { 0x42, 0x61, 0x51, 0x49, 0x46 }, { 0x21, 0x41, 0x45, 0x4B, 0x31 },
    { 0x18, 0x14, 0x12, 0x7F, 0x10 }, { 0x27, 0x45, 0x45, 0x45, 0x39 },
    { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, { 0x01, 0x71, 0x09, 0x05, 0x03 },
    { 0x36, 0x49, 0x49, 0x49, 0x36 }, { 0x06, 0x49, 0x49, 0x29, 0x1E }
    }, {   /* Style 3: square bold */
    { 0x7F, 0x7F, 0x41, 0x7F, 0x7F }, { 0x00, 0x00, 0x7F, 0x7F, 0x00 },
    { 0x79, 0x79, 0x49, 0x4F, 0x4F }, { 0x49, 0x49, 0x49, 0x7F, 0x7F },
    { 0x0F, 0x0F, 0x08, 0x7F, 0x7F }, { 0x4F, 0x4F, 0x49, 0x79, 0x79 },
    { 0x7F, 0x7F, 0x49, 0x79, 0x79 }, { 0x01, 0x01, 0x01, 0x7F, 0x7F },
    { 0x7F, 0x7F, 0x49, 0x7F, 0x7F }, { 0x4F, 0x4F, 0x49, 0x7F, 0x7F }
    }
};

#define Z2_1 0
#define Z2_2 6
#define Z2_T 12
#define Z2_3 14
#define Z2_4 20
#define DP2  0x36           /* colon: rows 2-3 and 5-6, two LEDs high each */

static void gross5(uint8_t ab, uint8_t z)
{
    uint8_t i;

    uint8_t st = (uint8_t)(schrift >= 1 && schrift <= 3 ? schrift - 1 : 0);

    for (i = 0; i < 5; i++)
        zb[ab + i] = ZIFFER5[st][z][i];
}

static void anzeige_t2(uint8_t a, uint8_t b, uint8_t trenner)
{
    leeren();
    gross5(Z2_1, (uint8_t)(a / 10));
    gross5(Z2_2, (uint8_t)(a % 10));
    zb[Z2_T] = (uint8_t)(trenner == DOPPELPUNKT ? DP2 : trenner);
    gross5(Z2_3, (uint8_t)(b / 10));
    gross5(Z2_4, (uint8_t)(b % 10));
}



/* ---------------------------------------------------------------- Menu */


/* ---------------------------------------------------------- Scrolling text */

/* Text arrives as UTF-8 via the UART into the text buffer, is converted
 * into a column stream (variable character width) and then shifted column
 * by column through the 25-column window. */
#define TEXT_MAX       256
#define STROM_MAX     1200
#define LEER_BREITE      3      /* space, plus one gap after it */

static __xdata uint8_t textpuffer[TEXT_MAX];
static __xdata uint8_t strom[STROM_MAX];
static uint16_t strom_len;

static void strom_spalte(uint8_t sp)
{
    if (strom_len < STROM_MAX)
        strom[strom_len++] = sp;
}

/* Text font according to the Schrift setting: 2 = the table as it is (thin
 * strokes), 1 and 3 = bold (each column ORed with its left neighbour,
 * strokes 2 pixels wide). Generated during build-up instead of separate
 * tables, which saves about 1 KB. A square variant for 3 filled the inner
 * spaces of a, e, 0 at seven rows height - unreadable. */
static void glyphe_anhaengen(uint8_t idx)
{
    uint8_t links = 0, rechts = 4, i, n = 0;
    uint8_t sp[7];

    if (idx == 0) {                         /* space */
        for (i = 0; i < LEER_BREITE; i++)
            strom_spalte(0);
        strom_spalte(0);
        return;
    }
    while (links < 4 && SCHRIFT[idx][links] == 0)
        links++;
    while (rechts > links && SCHRIFT[idx][rechts] == 0)
        rechts--;
    for (i = links; i <= rechts; i++)
        sp[n++] = SCHRIFT[idx][i];
    if (schrift != 2) {
        sp[n] = 0;
        for (i = n; i > 0; i--)             /* bold: column |= left neighbour */
            sp[i] |= sp[i - 1];
        n++;
    }
    for (i = 0; i < n; i++)
        strom_spalte(sp[i]);
    strom_spalte(0);
}

/* Reads one character from the UTF-8 text and returns the table index.
 * Known are ASCII and Ä Ö Ü ä ö ü ß degree euro section sign; everything
 * else becomes a single '?', even if it is several bytes long. */
static uint8_t utf8_glyphe(uint16_t *i, uint16_t n)
{
    uint8_t c = textpuffer[*i], d;

    (*i)++;
    if (c >= 0x20 && c <= 0x7E)
        return (uint8_t)(c - 0x20);

    if (c == 0xC3 && *i < n) {
        d = textpuffer[*i];
        (*i)++;
        switch (d) {
        case 0x84: return G_AE;
        case 0x96: return G_OE;
        case 0x9C: return G_UE;
        case 0xA4: return G_ae;
        case 0xB6: return G_oe;
        case 0xBC: return G_ue;
        case 0x9F: return G_SS;
        default:   return (uint8_t)('?' - 0x20);
        }
    }
    if (c == 0xC2 && *i < n) {
        d = textpuffer[*i];
        (*i)++;
        if (d == 0xB0) return G_GRAD;
        if (d == 0xA7) return G_PARA;
        return (uint8_t)('?' - 0x20);
    }
    if (c == 0xE2 && *i + 1 < n && textpuffer[*i] == 0x82
        && textpuffer[*i + 1] == 0xAC) {
        *i += 2;
        return G_EURO;
    }
    /* Unknown: swallow continuation bytes (10xxxxxx), output one '?' */
    while (*i < n && (textpuffer[*i] & 0xC0) == 0x80)
        (*i)++;
    return (uint8_t)('?' - 0x20);
}

static void text_laden(uint16_t n)
{
    uint16_t i = 0;

    strom_len = 0;
    while (i < n)
        glyphe_anhaengen(utf8_glyphe(&i, n));
}

/* Window at step off: column 24 shows strom[off], column 0 shows
 * strom[off-24]. At off = 0 the first column enters on the right. */
static void fenster(uint16_t off)
{
    uint8_t c;
    int16_t pos;

    for (c = 0; c < SPALTEN; c++) {
        pos = (int16_t)off + c - 24;
        bild[c] = (pos < 0 || pos >= (int16_t)strom_len) ? 0 : strom[pos];
    }
}

/* Run through once. The UART is dead meanwhile, P3.0/P3.1 are columns. */
/* A frame now takes 25 time slots, measured 5.2 ms, with and without other
 * tasks. 14 frames per column are 73 ms, about 13.7 columns per second. */
#define SCHRITT_FRAMES  14

#define M_ZEIT    0
#define M_DATUM   1
#define M_ALARM   2
#define M_KLANG   3
#define M_QUIZ    4
#define M_TIMER   5
#define M_THEMA   6
#define M_FONT    7
#define M_ANIM    8
#define M_ZIFFER  9
#define M_PUNKT   10
#define M_INFO    11
#define M_HELL    12
#define M_LED     13
#define M_TON     14
#define M_NACHT   15
#define M_MIKRO   16
#define M_ORGEL   17
#define M_UPDATE  18
#define M_ENDE    19
#define MENUEZAHL 20


static uint8_t ton_an = 1;

/* ---------------------------------------------------------------- Run */

#define ANSICHT_ZEIT  0
#define ANSICHT_DATUM 1
#define ANSICHT_TEMP  2

static uint8_t ansicht = ANSICHT_ZEIT;

/* Selected theme, 1 or 2. It lives in register 0x0B of the clock (alarm 2,
 * minutes): alarm 2 is disabled, the byte is not needed otherwise, and the
 * backup battery keeps it through a power failure. The controller itself
 * remembers nothing. */
static uint8_t theme = 1;

/* -------------------------------------------------------------- Settings
 *
 * In the STC8's EEPROM (IAP, sector 0). Theme, melody and night mode used to
 * live in free registers of the DS3231 - those are not enough for the
 * planned settings. On the first start with an empty EEPROM the values are
 * taken over from there. flash.sh leaves the EEPROM untouched when flashing.
 *
 * Writing means: erase the sector (about 5 ms, the CPU stalls that long -
 * the display pauses for a moment) and write it again. That only happens
 * when someone changes a setting. */
__sfr __at (0xC2) IAP_DATA;
__sfr __at (0xC3) IAP_ADDRH;
__sfr __at (0xC4) IAP_ADDRL;
__sfr __at (0xC5) IAP_CMD;
__sfr __at (0xC6) IAP_TRIG;
#define IAP_EIN    0x82         /* IAPEN, wait time for up to 20 MHz */
#define E_KENNUNG  0xA6
#define E_ZAHL     16

static uint8_t raetsel = 1;     /* alarm puzzle: 0 off, 1 sequence, 2 arithmetic */


static void iap_los(uint8_t cmd, uint16_t a)
{
    __bit ea = EA;

    IAP_CONTR = IAP_EIN;
    IAP_CMD = cmd;
    IAP_ADDRL = (uint8_t)a;
    IAP_ADDRH = (uint8_t)(a >> 8);
    EA = 0;                     /* the two trigger bytes must follow immediately */
    IAP_TRIG = 0x5A;
    IAP_TRIG = 0xA5;
#ifndef ZWILLING
    __asm
        nop
        nop
    __endasm;
#endif
    EA = ea;
}

static void iap_fertig(void)
{
    IAP_CONTR = 0;
    IAP_CMD = 0;
    IAP_TRIG = 0;
    IAP_ADDRH = 0x80;           /* park outside the EEPROM */
    IAP_ADDRL = 0;
}

static uint8_t iap_lesen(uint16_t a)
{
#ifdef ZWILLING
    return zw_iap[a & 0x7FFF];
#endif
    uint8_t d;

    iap_los(1, a);
    d = IAP_DATA;
    iap_fertig();
    return d;
}

static void iap_schreiben(uint16_t a, uint8_t d)
{
#ifdef ZWILLING
    zw_iap[a & 0x7FFF] &= d; return;
#endif
    IAP_DATA = d;
    iap_los(2, a);
    iap_fertig();
}

static void iap_loeschen(uint16_t a)
{
#ifdef ZWILLING
    { uint16_t i; for (i = 0; i < 512; i++) zw_iap[(a & 0x7E00) + i] = 0xFF; } return;
#endif
    iap_los(3, a);
    iap_fertig();
}

/* While IAP erases or writes, the CPU stalls - including the display
 * interrupt (erase about 5 ms). The column that was just switched on stayed
 * on that long and flashed brightly (reported for "Ton"). So darken the
 * matrix first and wait for the end of the current time slot. */
static uint8_t iap_dunkel_vorher;

static void iap_dunkel(void)
{
    iap_dunkel_vorher = ausfuehrung;
    ausfuehrung = 1;
#ifndef ZWILLING
    if (aktiv)
        while (!slot_dunkel)
            ;
#endif
}

static void iap_hell(void)
{
    ausfuehrung = iap_dunkel_vorher;
}

static void einst_speichern(void)
{
    uint8_t e[E_ZAHL], i;

    e[0] = E_KENNUNG;
    e[1] = theme;
    e[2] = melodie;
    e[3] = nacht_modus;
    e[4] = ton_an;
    e[5] = led_eff;
    e[6] = raetsel;
    e[7] = wechsel;
    e[8] = ziffer_eff;
    e[9] = dpunkt;
    e[10] = info_int;
    e[11] = hell_fest;
    e[12] = schrift;
    e[13] = mikro;
    for (i = 14; i < E_ZAHL; i++)
        e[i] = 0;
    iap_dunkel();
    iap_loeschen(0);
    for (i = 0; i < E_ZAHL; i++)
        iap_schreiben(i, e[i]);
    iap_hell();
}

static uint8_t einst_laden(void)
{
    uint8_t v;

    if (iap_lesen(0) != E_KENNUNG)
        return 0;
    v = iap_lesen(1); if (v >= 1 && v <= 3) theme = v;
    v = iap_lesen(2); if (v < MELODIEZAHL) melodie = v;
    v = iap_lesen(3); nacht_modus = (uint8_t)(v == 1);
    v = iap_lesen(4); ton_an = (uint8_t)(v != 0);
    v = iap_lesen(5); if (v <= 3) led_eff = v;
    v = iap_lesen(6); if (v <= 2) raetsel = v;
    v = iap_lesen(7); if (v <= 8) wechsel = v;
    v = iap_lesen(8); if (v <= 5) ziffer_eff = v;
    v = iap_lesen(9); if (v >= 1 && v <= 5) dpunkt = v;
    v = iap_lesen(10); if (v <= 5) info_int = v;
    v = iap_lesen(11); if (v <= 8) hell_fest = v;
    v = iap_lesen(12); if (v >= 1 && v <= 3) schrift = v;
    v = iap_lesen(13); if (v >= MIKRO_MIN && v <= MIKRO_MAX) mikro = v;
    return 1;
}

static void theme_speichern(void)
{
    einst_speichern();
}
static uint8_t zeit[7];

static uint8_t test_halten;
static uint8_t lauf_aktiv, lauf_k;    /* scrolling text in the main loop */
static uint16_t lauf_off;

/* countdown: seconds, counted at the clock's second change (anzeige.c) */
static uint16_t count_rest;
static uint8_t count_aktiv, count_ende;

#include "anzeige.c"

static void bild_bauen(void)
{
    if (test_halten || lauf_aktiv || orgel_aktiv)
        return;
    anzeige_sofort();
}

/* ----------------------------------------------------------- Light organ
 *
 * Matrix black; every noise throws a "stone into the water": three rings
 * run from the centre outwards, always completely, regardless of the
 * volume. The rear rings vanish earlier (decay). Up to four waves at the
 * same time; a new one only starts when the youngest has travelled a bit,
 * so that one clap does not trigger several.
 *
 * Microphone: a lit wave raises the microphone itself - 19 without ESP, 46
 * with an ESP on RX/TX (quiet: 30). With the rear LEDs lit it reached 72.
 * Above the threshold a wave triggers the next one, with an ESP an endless
 * chain (microphone at 4095). Hence: threshold "Mikro" default 50, rear LEDs
 * dark during the organ, and a dead time after every wave. Clapping: 47-148. */
#define ORGEL_SCHWELLE ((uint16_t)(mikro * 10u))
/* Dead time after each wave: the microphone is ignored for this many frames.
 * Measured 2026-11-14: a lit wave raises the microphone itself (19 with the
 * rear LEDs off, 72 with them on), so a wave could trigger the next one. */
#define ORGEL_TOTZEIT  90       /* frames, about 0.5 s */
#define ORGEL_SCHRITT  5        /* frames per half pixel of radius, about 28 ms */
#define WELLEN         4
#define RING_ABSTAND   6        /* half pixels between the rings of a wave */
#define RADIUS_MAX     25       /* 2 x distance centre - corner */
#define WELLE_FREI     0xFF

/* 2 x distance of each pixel to the centre (column 13, row 4), rounded;
 * computed on the PC so that the 8051 does not take square roots. */
static const __code uint8_t ABSTAND2[7][25] = {
    { 25, 23, 21, 19, 17, 15, 13, 12, 10,  8,  7,  6,  6,  6,  7,  8, 10, 12, 13, 15, 17, 19, 21, 23, 25 },
    { 24, 22, 20, 18, 16, 15, 13, 11,  9,  7,  6,  4,  4,  4,  6,  7,  9, 11, 13, 15, 16, 18, 20, 22, 24 },
    { 24, 22, 20, 18, 16, 14, 12, 10,  8,  6,  4,  3,  2,  3,  4,  6,  8, 10, 12, 14, 16, 18, 20, 22, 24 },
    { 24, 22, 20, 18, 16, 14, 12, 10,  8,  6,  4,  2,  0,  2,  4,  6,  8, 10, 12, 14, 16, 18, 20, 22, 24 },
    { 24, 22, 20, 18, 16, 14, 12, 10,  8,  6,  4,  3,  2,  3,  4,  6,  8, 10, 12, 14, 16, 18, 20, 22, 24 },
    { 24, 22, 20, 18, 16, 15, 13, 11,  9,  7,  6,  4,  4,  4,  6,  7,  9, 11, 13, 15, 16, 18, 20, 22, 24 },
    { 25, 23, 21, 19, 17, 15, 13, 12, 10,  8,  7,  6,  6,  6,  7,  8, 10, 12, 13, 15, 17, 19, 21, 23, 25 }
};
static __xdata uint8_t welle_r[WELLEN];
static uint8_t orgel_k, orgel_tot;

static void orgel_start(void)
{
    uint8_t w;

    for (w = 0; w < WELLEN; w++)
        welle_r[w] = WELLE_FREI;
    leeren();
    orgel_k = 0;
    orgel_tot = 0;
    orgel_aktiv = 1;
}

static void orgel_ende(void)
{
    orgel_aktiv = 0;
    bild_bauen();
}

static void orgel_welle(void)
{
    uint8_t w, frei = WELLE_FREI, jung = WELLE_FREI;

    for (w = 0; w < WELLEN; w++) {
        if (welle_r[w] == WELLE_FREI)
            frei = w;
        else if (welle_r[w] < jung)
            jung = welle_r[w];
    }
    if (frei != WELLE_FREI && jung >= RING_ABSTAND) {
        welle_r[frei] = 0;
        orgel_tot = ORGEL_TOTZEIT;
    }
}

static void orgel_schritt(void)
{
    uint8_t w, x, y, k, d, r, spalte, rest;

    for (w = 0; w < WELLEN; w++)
        if (welle_r[w] != WELLE_FREI
            && ++welle_r[w] > RADIUS_MAX + 2 * RING_ABSTAND + 2)
            welle_r[w] = WELLE_FREI;
    for (x = 0; x < SPALTEN; x++) {
        spalte = 0;
        for (y = 0; y < 7; y++) {
            d = ABSTAND2[y][x];
            for (w = 0; w < WELLEN; w++) {
                r = welle_r[w];
                if (r == WELLE_FREI)
                    continue;
                for (k = 0; k < 3; k++) {
                    if (r < k * RING_ABSTAND)
                        break;
                    rest = (uint8_t)(r - k * RING_ABSTAND);
                    /* ring k is 1 pixel wide and decays earlier */
                    if (rest >= d && rest - d <= 1 && d <= RADIUS_MAX - 4 * k)
                        spalte |= (uint8_t)(1u << y);
                }
            }
        }
        bild[x] = spalte;
    }
}

static void piep(void)
{
    uint8_t i;
    volatile uint16_t w;

    if (!ton_an)
        return;
    buzzer(1);
    for (i = 0; i < 12; i++)
        for (w = 0; w < 400; w++)
            ;
    buzzer(0);
}

static void tasten_flanke(uint8_t *kn, uint8_t *dn)
{
    uint8_t k, d;

    touch_lesen(&k, &d);
    *kn = (uint8_t)(k && !war_kreis);
    *dn = (uint8_t)(d && !war_dreieck);
    war_kreis = k;
    war_dreieck = d;
}

/* Generic value editor for three two-digit fields.
 * Triangle increments, circle moves to the next field; after the last one
 * it ends. The selected field blinks. */
/* ---------------------------------------------------------------- Menu
 *
 * Texts in the 5x7 font of the scrolling text; whatever does not fit into
 * the 25 columns scrolls. Each item shows its current value in plain text
 * ("Quiz: Rechnen"), "aus" instead of 0. One minute without input: back to
 * the clock, leaving sub-items without saving (as requested by the user). */
#define MENUE_LEER 10700u       /* 1 minute in frames */

static uint8_t menue_abbruch;   /* sub-item left because of timeout */
static uint16_t tp_n;           /* fill level of textpuffer */
static uint16_t mt_off;
static uint8_t mt_k, mt_halt;

static void tp_add(const char *t)
{
    while (*t && tp_n < TEXT_MAX)
        textpuffer[tp_n++] = (uint8_t)*t++;
}

static void tp_zahl2(uint8_t v)
{
    textpuffer[tp_n++] = (uint8_t)('0' + v / 10);
    textpuffer[tp_n++] = (uint8_t)('0' + v % 10);
}

/* show textpuffer[0..tp_n) as menu text: stands still if it fits */
static void mt_start(void)
{
    text_laden(tp_n);
    mt_off = 24;
    mt_k = 0;
    mt_halt = 120;              /* stand a good 0.6 s before scrolling */
}

static void mt_zeichnen(void)
{
    uint8_t i, ab;

    if (strom_len <= SPALTEN) {
        ab = (uint8_t)((SPALTEN - strom_len) / 2);
        for (i = 0; i < SPALTEN; i++)
            bild[i] = (i >= ab && i < ab + strom_len) ? strom[i - ab] : 0;
        return;
    }
    fenster(mt_off);
    if (mt_halt) {
        mt_halt--;
        return;
    }
    if (++mt_k >= SCHRITT_FRAMES) {
        mt_k = 0;
        if (++mt_off > strom_len + 30) {
            mt_off = 24;
            mt_halt = 120;
        }
    }
}

static void mt_wort(const char *t)
{
    tp_n = 0;
    tp_add(t);
    mt_start();
}

static void editor(uint8_t *wert, const __code uint8_t *klein_,
                   const __code uint8_t *gross_, uint8_t trenner)
{
    uint8_t feld = 0, kn, dn, blink = 0, a, b2;
    uint16_t z = 0, leer = 0;

    while (feld < 3) {
        if (++leer > MENUE_LEER) {
            menue_abbruch = 1;
            return;
        }
        anzeige_t1(wert[0], wert[1], wert[2], trenner);
        if (blink < 8) {
            a = (uint8_t)(feld == 0 ? G1 : (feld == 1 ? G3 : K1));
            b2 = (uint8_t)(feld == 0 ? G2 : (feld == 1 ? G4 : K2));
            bild[a] = 0; bild[a+1] = 0; bild[a+2] = 0;
            bild[b2] = 0; bild[b2+1] = 0; bild[b2+2] = 0;
        }
        rahmen();
        if (++z % 12 == 0)
            blink = (uint8_t)((blink + 1) & 0x0F);
        if (z % 25)
            continue;
        tasten_flanke(&kn, &dn);
        if (kn || dn)
            leer = 0;
        if (dn) {
            wert[feld]++;
            if (wert[feld] > gross_[feld])
                wert[feld] = klein_[feld];
            piep();
        }
        if (kn) {
            feld++;
            piep();
        }
    }
}

static const __code uint8_t KL_ZEIT[3]  = { 0, 0, 0 };
static const __code uint8_t GR_ZEIT[3]  = { 23, 59, 59 };
static const __code uint8_t KL_DATUM[3] = { 1, 1, 0 };
static const __code uint8_t GR_DATUM[3] = { 31, 12, 99 };
static const __code uint8_t KL_ALARM[3] = { 0, 0, 0 };
static const __code uint8_t GR_ALARM[3] = { 1, 23, 59 };

/* Alarm setting in two steps.
 *
 * The alarm used to go through the three-field editor: the first field was
 * on/off and appeared as "00" or "01", followed by hour and minute -
 * together "01:07:30", which looked like a date, not a time. Now first AN or
 * AUS in letters, then the time as hh:mm in the large font. */
static const __code uint8_t GR_HM[2]   = { 23, 59 };
static const __code uint8_t GR_TIMER[2] = { 99, 59 };

static uint8_t an_aus_waehlen(uint8_t an)
{
    uint8_t kn, dn;
    uint16_t z = 0, leer = 0;

    mt_wort(an ? "an" : "aus");
    for (;;) {
        mt_zeichnen();
        rahmen();
        if (++leer > MENUE_LEER) {
            menue_abbruch = 1;
            return an;
        }
        if (++z % 10)
            continue;
        tasten_flanke(&kn, &dn);
        if (dn) {
            an = (uint8_t)!an;
            mt_wort(an ? "an" : "aus");
            leer = 0;
            piep();
        }
        if (kn) {
            piep();
            return an;
        }
    }
}

/* Two fields hh:mm in the large font, the selected one blinks. */
static void editor2(uint8_t *wert, const __code uint8_t *gross_)
{
    uint8_t feld = 0, kn, dn, blink = 0, i, lo, hi;
    uint16_t z = 0, leer = 0;

    while (feld < 2) {
        if (++leer > MENUE_LEER) {
            menue_abbruch = 1;
            return;
        }
        anzeige_t2(wert[0], wert[1], DOPPELPUNKT);
        if (blink < 8) {
            lo = (uint8_t)(feld == 0 ? Z2_1 : Z2_3);
            hi = (uint8_t)(feld == 0 ? Z2_2 + 4 : Z2_4 + 4);
            for (i = lo; i <= hi; i++)
                bild[i] = 0;
        }
        rahmen();
        if (++z % 12 == 0)
            blink = (uint8_t)((blink + 1) & 0x0F);
        if (z % 25)
            continue;
        tasten_flanke(&kn, &dn);
        if (kn || dn)
            leer = 0;
        if (dn) {
            wert[feld]++;
            if (wert[feld] > gross_[feld])
                wert[feld] = 0;
            piep();
        }
        if (kn) {
            feld++;
            piep();
        }
    }
}

static void alarm_schreiben(uint8_t an, uint8_t std, uint8_t min)
{
    rtc_schreiben(0x07, 0x00);              /* second 0, A1M1 = 0 */
    rtc_schreiben(0x08, zubcd(min));
    rtc_schreiben(0x09, zubcd(std));
    /* A1M4 = 1: day does not matter. The DS3231 then ignores the day bits -
     * bit 0 holds the alarm enable. */
    rtc_schreiben(0x0A, (uint8_t)(0x80 | (an ? 0x01 : 0x00)));
    alarm_an = (uint8_t)(an ? 1 : 0);
    rtc_schreiben(0x0E, TON_REG[TON_AUS]);  /* A1IE off, buzzer off */
    rtc_schreiben(0x0F, 0x00);              /* clear flag */
}

/* Ten-millisecond tick via timer 1, for delays with the display switched
 * off (update mode). Timer 0 belongs to scanning. 16 bit, 12T, reload value
 * 0xC400 = 15360 ticks of 0.651 us each. */
static void takt_init(void)
{
    AUXR &= 0xBF;               /* timer 1 in 12T mode */
    TMOD &= 0x0F;               /* mode 0: 16 bit with reload */
    TL1 = 0x00;
    TH1 = 0xC4;
    TF1 = 0;
    TR1 = 1;
}

/* UPDATE: fallback for uploading new firmware.
 *
 * Phase 1, 3 seconds: the text is shifted two columns to the right - the
 * first two columns are P3.0 and P3.1, i.e. the UART, and explicitly stay
 * dark.
 *
 * Phase 2: the matrix goes completely off, the rear LEDs stay lit as a sign
 * that the mode is alive. Reason: measurement. With the display running a
 * single character arrived in only 38 % of cases, although P3.0 and P3.1
 * were not driven - scanning the other columns disturbs reception. With the
 * display dark every character arrives. The rear LEDs are DC and do not
 * disturb.
 *
 * An 'R' starts the bootloader immediately; circle or triangle end the mode.
 * For checking, 'b' outputs the frame buffer as hex. */
static const __code uint8_t TXT_UPDATE[6] = { BU_U, BU_P, BU_D, BU_A, BU_T, BU_E };

static void update_modus(void)
{
    uint8_t i, j, kn, dn, c, dunkel = 0, ticks = 0;
    uint16_t z = 0;

    uart_init();
    while (uart_avail())
        uart_getc();
    takt_init();
    TF1 = 0;
    uart_frei = 2;
    leeren();
    for (i = 0; i < 6; i++)
        for (j = 0; j < 3; j++)
            bild[2 + TEXTPOS[i] + j] = BUCHSTABE[TXT_UPDATE[i]][j];

    for (;;) {
        if (!dunkel) {
            rahmen();
            if (++z >= 640) {                   /* about 3 s at 4.7 ms per frame */
                dunkel = 1;
                anzeige_aus();
                uart_init();
                P2M1 &= 0x7F;                   /* P2.7 push-pull high: rear LEDs on */
                P2M0 |= 0x80;
                P2 |= 0x80;
                TF1 = 0;
            }
        }
        if (uart_avail()) {
            c = uart_getc();
            if (c == 'R') {
                anzeige_aus();
                RESET_IN_BOOTLOADER();
            } else if (c == 'b') {
                for (i = 0; i < SPALTEN; i++) {
                    uart_putc("0123456789ABCDEF"[bild[i] >> 4]);
                    uart_putc("0123456789ABCDEF"[bild[i] & 0x0F]);
                }
                uart_puts("\r\n");
            }
        }
        if (!dunkel) {
            if (z % 25)
                continue;
        } else {
            if (!TF1)
                continue;
            TF1 = 0;
            if (++ticks < 10)                   /* keys every 100 ms */
                continue;
            ticks = 0;
        }
        tasten_flanke(&kn, &dn);
        if (kn || dn)
            break;
    }
    anzeige_aus();
    uart_frei = 0;
}

static const char * const __code MENUE_NAME[MENUEZAHL] = {
    "Zeit", "Datum", "Alarm", "Klang", "Quiz", "Timer", "Thema", "Schrift",
    "Anim", "Ziffern", "Punkt", "Info", "Hell", "LED", "Ton", "Nacht",
    "Mikro", "Orgel", "Update", "Ende"
};
static const char * const __code W_AN_AUS[2] = { "aus", "an" };
static const char * const __code W_QUIZ[3]   = { "aus", "Code", "Rechnen" };
static const char * const __code W_KLANG[5]  = { "Pip", "Steigend", "Triller", "T3 1 kHz", "T3 520 Hz" };
static const char * const __code W_THEMA[3]  = { "H:M:S", "HH:MM", "HH:MM s" };
static const char * const __code W_ANIM[9]   = { "aus", "Schieben", "Pixel", "Spalten", "Oben",
                                                 "Mitte", "Rollen", "Auf-Ab", "Wischen" };
static const char * const __code W_ZIFF[6]   = { "aus", "Aufbau", "Fallen", "Pixel", "Auf-Ab", "Mitte" };
static const char * const __code W_PUNKT[5]  = { "Mitte", "Blenden", "Konstant", "Auf-Ab", "Sekunde" };
static const char * const __code W_INFO[6]   = { "5 s", "10 s", "30 s", "1 min", "2 min", "aus" };
static const char * const __code W_LED[4]    = { "aus", "konstant", "Ton", "Atmen" };

/* Menu text: with name ("Info: 30 s") or just the value ("30 s") - while
 * stepping through the options only the value, the name returns after 3 s. */
static void menue_text_art(uint8_t punkt, uint8_t mit_name)
{
    uint8_t roh[2];

    tp_n = 0;
    if (punkt == M_ZEIT || punkt == M_DATUM || punkt == M_TIMER || punkt >= M_ORGEL) {
        tp_add(MENUE_NAME[punkt]);
        mt_start();
        return;
    }
    if (mit_name) {
        tp_add(MENUE_NAME[punkt]);
        tp_add(": ");
    }
    switch (punkt) {
    case M_ALARM:
        if (alarm_an && rtc_lesen(0x08, roh, 2)) {
            tp_zahl2(bcd((uint8_t)(roh[1] & 0x3F)));
            tp_add(":");
            tp_zahl2(bcd((uint8_t)(roh[0] & 0x7F)));
        } else {
            tp_add("aus");
        }
        break;
    case M_KLANG:  tp_add(W_KLANG[melodie]); break;
    case M_QUIZ:   tp_add(W_QUIZ[raetsel]); break;
    case M_THEMA:  tp_add(W_THEMA[theme - 1]); break;
    case M_FONT:   textpuffer[tp_n++] = (uint8_t)('0' + schrift); break;
    case M_ANIM:   tp_add(W_ANIM[wechsel]); break;
    case M_ZIFFER: tp_add(W_ZIFF[ziffer_eff]); break;
    case M_PUNKT:  tp_add(W_PUNKT[dpunkt - 1]); break;
    case M_INFO:   tp_add(W_INFO[info_int]); break;
    case M_HELL:
        if (hell_fest)
            textpuffer[tp_n++] = (uint8_t)('0' + hell_fest);
        else
            tp_add("Auto");
        break;
    case M_LED:    tp_add(W_LED[led_eff]); break;
    case M_TON:    tp_add(W_AN_AUS[ton_an]); break;
    case M_NACHT:  tp_add(W_AN_AUS[nacht_modus]); break;
    case M_MIKRO:  tp_zahl2((uint8_t)(mikro * 10)); break;
    default: break;
    }
    mt_start();
}

static void menue_text(uint8_t punkt)
{
    menue_text_art(punkt, 1);
}

/* Set timer: holding the triangle counts up - seconds first, faster and
 * faster the longer it is held, then in 10 s and finally in minutes.
 * Circle starts. Begins at 0:00 (as requested by the user). */
static uint16_t timer_editor(void)
{
    uint16_t sek = 0, leer = 0, halte = 0;
    uint8_t k, d, z = 0;

    touch_lesen(&war_kreis, &war_dreieck);
    for (;;) {
        anzeige_t2((uint8_t)(sek / 60), (uint8_t)(sek % 60), DOPPELPUNKT);
        rahmen();
        if (++leer > MENUE_LEER) {
            menue_abbruch = 1;
            return 0;
        }
        if (++z % 5)                            /* keys every 28 ms */
            continue;
        touch_lesen(&k, &d);
        if (k && !war_kreis) {
            war_kreis = k;
            piep();
            return sek;
        }
        war_kreis = k;
        if (!d) {
            halte = 0;
            continue;
        }
        leer = 0;
        halte++;
        if (halte == 1)
            sek++;                              /* tap: one second */
        else if (halte < 54)                    /* up to 1.5 s: every 0.2 s */
            sek = (uint16_t)(sek + (halte % 7 == 0));
        else if (halte < 107)                   /* up to 3 s: every 0.1 s */
            sek = (uint16_t)(sek + (halte % 4 == 0));
        else if (halte < 180) {                 /* up to 5 s: in 10 s, 5 per second */
            if (halte % 7 == 0)
                sek = (uint16_t)((sek / 10 + 1) * 10);
        } else if (halte % 10 == 0) {           /* after that: minutes, a good 3 per second */
            sek = (uint16_t)((sek / 60 + 1) * 60);
        }
        if (sek > 5999)
            sek = 0;
    }
}

/* Choose the font: the time shows the candidate, it is only saved when
 * confirmed with the circle. */
static void font_editor(void)
{
    uint8_t alt = schrift, kn, dn, z = 0;
    uint16_t leer = 0;

    touch_lesen(&war_kreis, &war_dreieck);
    rtc_lesen(0x00, zeit, 7);
    for (;;) {
        schirm_zeichnen(S_ZEIT);
        rahmen();
        if (++leer > MENUE_LEER) {
            schrift = alt;
            menue_abbruch = 1;
            return;
        }
        if (leer % 90 == 0)
            rtc_lesen(0x00, zeit, 7);
        if (++z % 10)
            continue;
        tasten_flanke(&kn, &dn);
        if (dn) {
            schrift = (uint8_t)(schrift >= 3 ? 1 : schrift + 1);
            leer = 0;
            piep();
        }
        if (kn) {
            einst_speichern();
            piep();
            return;
        }
    }
}

static void menue(void)
{
    uint8_t punkt = 0, kn, dn, wert[3], roh[4], an, z = 0;
    uint16_t leer = 0, name_in = 0;

    menue_text(punkt);
    for (;;) {
        mt_zeichnen();
        rahmen();
        if (++leer > MENUE_LEER)
            return;
        if (name_in && --name_in == 0)
            menue_text(punkt);              /* with name again after 3 s */
        if (++z % 10)
            continue;
        tasten_flanke(&kn, &dn);
        if (dn) {
            punkt = (uint8_t)((punkt + 1) % MENUEZAHL);
            menue_text(punkt);
            name_in = 0;
            leer = 0;
            piep();
        }
        if (!kn)
            continue;
        leer = 0;
        piep();
        menue_abbruch = 0;
        switch (punkt) {
        case M_ZEIT:
            rtc_lesen(0x00, zeit, 7);
            wert[0] = bcd((uint8_t)(zeit[2] & 0x3F));
            wert[1] = bcd(zeit[1]);
            wert[2] = bcd(zeit[0]);
            editor(wert, KL_ZEIT, GR_ZEIT, DOPPELPUNKT);
            if (menue_abbruch)
                return;
            rtc_schreiben(0x00, zubcd(wert[2]));
            rtc_schreiben(0x01, zubcd(wert[1]));
            rtc_schreiben(0x02, zubcd(wert[0]));
            break;
        case M_DATUM:
            rtc_lesen(0x00, zeit, 7);
            wert[0] = bcd(zeit[4]);
            wert[1] = bcd((uint8_t)(zeit[5] & 0x1F));
            wert[2] = bcd(zeit[6]);
            if (wert[0] < 1) wert[0] = 1;
            if (wert[1] < 1) wert[1] = 1;
            editor(wert, KL_DATUM, GR_DATUM, PUNKT);
            if (menue_abbruch)
                return;
            rtc_schreiben(0x04, zubcd(wert[0]));
            rtc_schreiben(0x05, zubcd(wert[1]));
            rtc_schreiben(0x06, zubcd(wert[2]));
            break;
        case M_ALARM:
            rtc_lesen(0x07, roh, 4);
            wert[0] = bcd((uint8_t)(roh[2] & 0x3F));
            wert[1] = bcd((uint8_t)(roh[1] & 0x7F));
            if (wert[0] > 23) wert[0] = 0;
            if (wert[1] > 59) wert[1] = 0;
            an = an_aus_waehlen(alarm_an);
            if (!menue_abbruch && an)
                editor2(wert, GR_HM);
            if (menue_abbruch)
                return;
            alarm_schreiben(an, wert[0], wert[1]);
            break;
        case M_KLANG:
            melodie = (uint8_t)((melodie + 1) % MELODIEZAHL);
            einst_speichern();
            menue_text(punkt);
            mt_zeichnen();
            melodie_spielen(melodie, 360, 1);   /* play briefly */
            break;
        case M_QUIZ:
            raetsel = (uint8_t)((raetsel + 1) % 3);
            einst_speichern();
            break;
        case M_TIMER:
            countdown_start(timer_editor());
            return;
        case M_THEMA:
            theme = (uint8_t)(theme >= 3 ? 1 : theme + 1);
            theme_speichern();
            break;
        case M_FONT:
            font_editor();
            if (menue_abbruch)
                return;
            break;
        case M_ANIM:
            wechsel = (uint8_t)((wechsel + 1) % 9);
            einst_speichern();
            menue_text_art(punkt, 0);
            mt_zeichnen();
            anim_vorfuehren();              /* show right away what it looks like */
            break;
        case M_ZIFFER:
            ziffer_eff = (uint8_t)((ziffer_eff + 1) % 6);
            einst_speichern();
            break;
        case M_PUNKT:
            dpunkt = (uint8_t)(dpunkt >= 5 ? 1 : dpunkt + 1);
            einst_speichern();
            break;
        case M_INFO:
            info_int = (uint8_t)((info_int + 1) % 6);   /* 5 = off */
            einst_speichern();
            break;
        case M_HELL:
            hell_fest = (uint8_t)((hell_fest + 1) % 9);
            if (hell_fest) {
                helligkeit = hell_fest;
                helligkeit_uebernehmen();
            } else {
                hell_gesetzt = 0;               /* automatic mode sets it again right away */
                helligkeit_regeln();
            }
            einst_speichern();
            break;
        case M_LED:
            led_eff = (uint8_t)((led_eff + 1) & 3);
            if (led_eff)
                led_merk = led_eff;
            einst_speichern();
            break;
        case M_TON:
            ton_an = (uint8_t)!ton_an;
            einst_speichern();
            break;
        case M_NACHT:
            nacht_modus = (uint8_t)!nacht_modus;
            einst_speichern();
            break;
        case M_MIKRO:
            mikro = (uint8_t)(mikro >= MIKRO_MAX ? MIKRO_MIN : mikro + 1);
            einst_speichern();
            break;
        case M_ORGEL:
            orgel_start();
            return;
        case M_UPDATE:
            update_modus();
            break;
        default:
            return;
        }
        menue_text_art(punkt, 0);           /* show only the new option */
        name_in = 535;
        z = 0;
    }
}

/* The alarm lives in the clock's alarm registers, not in the controller -
 * so it survives a power failure. Only the flag is checked here. */
static uint8_t alarm_faellig(void)
{
    uint8_t st;

    if (!alarm_an)
        return 0;
    if (!rtc_lesen(0x0F, &st, 1))
        return 0;
    return (uint8_t)(st & 0x01);
}

/* Alarm melodies: pairs of tone and duration in frames (about 5.6 ms each),
 * duration 0 = from the start. Each begins immediately with a tone - the
 * alarm used to begin with four seconds of silence. */
static const __code uint8_t MELODIE_1[] = {     /* classic: pip pip ... */
    TON_4K, 13, TON_AUS, 14, TON_4K, 13, TON_AUS, 125, 0, 0
};
static const __code uint8_t MELODIE_2[] = {     /* rising: three tones upwards */
    TON_1K, 22, TON_4K, 22, TON_8K, 22, TON_AUS, 90, 0, 0
};
static const __code uint8_t MELODIE_3[] = {     /* trill like a telephone */
    TON_4K, 9, TON_8K, 9, TON_4K, 9, TON_8K, 9, TON_4K, 9, TON_8K, 9,
    TON_4K, 9, TON_8K, 9, TON_4K, 9, TON_8K, 9, TON_4K, 9, TON_8K, 9,
    TON_4K, 9, TON_8K, 9, TON_4K, 9, TON_8K, 9, TON_4K, 9, TON_8K, 9,
    TON_AUS, 180, 0, 0
};
static const __code uint8_t MELODIE_4[] = {     /* T3 like a smoke alarm, 1.024 kHz:
                                                 * 3 x 0.5 s tone/0.5 s pause, 1.5 s pause */
    TON_1K, 89, TON_AUS, 89, TON_1K, 89, TON_AUS, 89, TON_1K, 89, TON_AUS, 134,
    TON_AUS, 134, 0, 0
};
static const __code uint8_t * const __code MELODIEN[4] = {
    MELODIE_1, MELODIE_2, MELODIE_3, MELODIE_4
};
static uint16_t ton_spaet;      /* measurement: toggles that came too late */

/* Melody 5: T3 pattern with real 520 Hz. The DS3231 cannot do 520 Hz. In
 * 1 Hz mode, however, its output is low for half a second at a time; if the
 * firmware toggles 1040 times per second during that time between 1 Hz mode
 * (pin follows the square wave, i.e. low) and INTCN (pin high), a 520 Hz
 * tone results. In the high half it stays silent - that automatically gives
 * 0.5 s tone, 0.5 s pause. Three seconds of toggling from a second change
 * hit exactly three low halves, whichever half is low; then 1 s of silence -
 * together the T3 pattern (3 x 0.5/0.5, then 1.5 s pause) in a 4 s cycle.
 * Toggle clock: timer 1, 0.9615 ms. */
#define HALB_520 17723u         /* 18.432 MHz / 1040 */

static uint8_t t3_520(uint16_t max_rahmen, uint8_t abbruch)
{
#ifdef ZWILLING
    {
        uint16_t n;
        uint8_t k, d;

        zw_ton_name("520 Hz T3");
        for (n = 0; n < max_rahmen; n++) {
            rahmen();
            if (abbruch && n % 20 == 0) {
                touch_lesen(&k, &d);
                if (k || d) { zw_ton_name("aus"); return 1; }
            }
        }
        zw_ton_name("aus");
        return 0;
    }
#endif
    uint16_t r0 = frames_gesamt, n;
    uint8_t s1, s2, k, d, w;

    ton_spaet = 0;
    TR1 = 0;
    AUXR |= 0x40;               /* timer 1 in 1T mode */
    TMOD &= 0x0F;               /* mode 0: 16 bit with reload */
    TL1 = (uint8_t)(65536u - HALB_520);
    TH1 = (uint8_t)((65536u - HALB_520) >> 8);
    for (;;) {
        /* wait for a second change */
        s1 = 0xFF;
        rtc_lesen(0x00, &s1, 1);
        for (w = 0; w < 250; w++) {
            s2 = s1;
            rtc_lesen(0x00, &s2, 1);
            if (s2 != s1)
                break;
        }
        i2c_schnell = 1;
        TF1 = 0;
        TR1 = 1;
        for (n = 0; n < 3120; n++) {
            while (!TF1)
                ;
            TF1 = 0;
            rtc_schreiben(0x0E, (uint8_t)((n & 1) ? 0x04 : 0x00));
            if (TF1)
                ton_spaet++;
            if (abbruch && n % 520 == 519) {    /* check for touch every 0.5 s */
                i2c_schnell = 0;
                touch_lesen(&k, &d);
                i2c_schnell = 1;
                if (k || d) {
                    TR1 = 0;
                    i2c_schnell = 0;
                    rtc_schreiben(0x0E, TON_REG[TON_AUS]);
                    return 1;
                }
                TF1 = 0;
            }
        }
        TR1 = 0;
        i2c_schnell = 0;
        rtc_schreiben(0x0E, TON_REG[TON_AUS]);
        if ((uint16_t)(frames_gesamt - r0) >= max_rahmen)
            return 0;
        if (abbruch) {
            touch_lesen(&k, &d);
            if (k || d)
                return 1;
        }
    }
}

/* Plays melody nr until max_rahmen have passed or - if abbruch - a key is
 * touched. The display keeps running meanwhile. Returns 1 if aborted. */
static uint8_t melodie_spielen(uint8_t nr, uint16_t max_rahmen, uint8_t abbruch)
{
    const __code uint8_t *m;
    uint16_t r = 0;
    uint8_t i = 0, n, k, d;

    if (nr == 4)
        return t3_520(max_rahmen, abbruch);
    m = MELODIEN[nr < 4 ? nr : 0];

    for (;;) {
        if (m[2 * i + 1] == 0) {
            i = 0;
            continue;
        }
        ton(m[2 * i]);
        for (n = 0; n < m[2 * i + 1]; n++) {
            rahmen();
            if (++r >= max_rahmen) {
                ton(TON_AUS);
                return 0;
            }
            if (abbruch && (r % 20) == 0) {
                touch_lesen(&k, &d);
                if (k || d) {
                    ton(TON_AUS);
                    return 1;
                }
            }
        }
        i++;
    }
}

/* ------------------------------------------------------------ Alarm puzzle
 *
 * Switching off only with the puzzle solved (setting QUIZ): 1 = repeat six
 * symbols of circle and triangle, 2 = arithmetic problem, 0 = off (one touch
 * is enough). The first touch stops the tone and shows the puzzle; 20 s
 * without input or a wrong answer switch it on again. After 10 minutes it
 * stops in any case. */
#define RAETSEL_LEER  3580      /* 20 s in frames */
#define WECKER_MAX    107000UL  /* 10 minutes in frames */

static const __code uint8_t SYM_KREIS[3]   = { 0x1C, 0x22, 0x1C };
static const __code uint8_t SYM_DREIECK[3] = { 0x70, 0x7E, 0x70 };
static const __code uint8_t SYM_PLUS[3]    = { 0x08, 0x1C, 0x08 };
static const __code uint8_t SYM_MINUS[3]   = { 0x08, 0x08, 0x08 };
static const __code uint8_t SYM_GLEICH[3]  = { 0x14, 0x14, 0x14 };
static const __code uint8_t SYM_ERLEDIGT[3] = { 0x00, 0x08, 0x00 };


static void symbol(uint8_t ab, const __code uint8_t *z)
{
    bild[ab] = z[0];
    bild[ab + 1] = z[1];
    bild[ab + 2] = z[2];
}

/* Show the puzzle once and let it be solved. 1 = solved, 0 = given up
 * (timeout or wrong answer). */
static uint8_t raetsel_folge(void)
{
    uint8_t folge = (uint8_t)(zufall() & 0x3F), stelle = 0, i, kn, dn, z = 0, falsch;
    uint16_t leer = 0;

    touch_lesen(&war_kreis, &war_dreieck);   /* the wake-up touch does not count */

    for (;;) {
        leeren();
        for (i = 0; i < 6; i++)
            symbol(TEXTPOS[i], i < stelle ? SYM_ERLEDIGT
                   : ((folge >> i) & 1) ? SYM_DREIECK : SYM_KREIS);
        rahmen();
        if (++leer > RAETSEL_LEER)
            return 0;
        if (++z % 10)
            continue;
        tasten_flanke(&kn, &dn);
        if (!kn && !dn)
            continue;
        leer = 0;
        falsch = (uint8_t)(kn && dn) || (dn != (uint8_t)((folge >> stelle) & 1));
        if (falsch) {
            for (i = 0; i < SPALTEN; i++)       /* flash briefly, new sequence */
                bild[i] = 0x7F;
            for (i = 0; i < 40; i++)
                rahmen();
            folge = (uint8_t)(zufall() & 0x3F);
            stelle = 0;
            continue;
        }
        piep();
        if (++stelle >= 6)
            return 1;
    }
}

static uint8_t raetsel_rechnen(void)
{
    /* Plus or minus with a one-digit result: the triangle counts through 0-9,
     * the circle confirms (as requested by the user). */
    uint8_t a, b, plus, soll, ziffer = 0, kn, dn, z = 0;
    uint16_t leer = 0;

    touch_lesen(&war_kreis, &war_dreieck);   /* the wake-up touch does not count */
    plus = (uint8_t)(zufall() & 1);
    do {
        a = (uint8_t)(zufall() % 10);
        b = (uint8_t)(zufall() % 10);
    } while (plus ? a + b > 9 : b > a);
    soll = (uint8_t)(plus ? a + b : a - b);
    for (;;) {
        leeren();
        gross(TEXTPOS[0], a);
        symbol(TEXTPOS[1], plus ? SYM_PLUS : SYM_MINUS);
        gross(TEXTPOS[2], b);
        symbol(TEXTPOS[3], SYM_GLEICH);
        if (z & 0x40)                           /* answer blinks */
            gross(TEXTPOS[4], ziffer);
        rahmen();
        if (++leer > RAETSEL_LEER)
            return 0;
        if (++z % 10)
            continue;
        tasten_flanke(&kn, &dn);
        if (!kn && !dn)
            continue;
        leer = 0;
        if (dn) {
            ziffer = (uint8_t)((ziffer + 1) % 10);
            z |= 0x40;                          /* visible right away */
            continue;
        }
        piep();
        return (uint8_t)(ziffer == soll);
    }
}

static void alarm_laeuten(void)
{
    uint32_t vergangen = 0;
    uint16_t f0, rest;

    rtc_schreiben(0x0F, 0x00);
    wecken();
    for (;;) {
        /* ring until someone touches or the 10 minutes are up */
        rest = (uint16_t)((WECKER_MAX - vergangen) > 60000UL ? 60000UL : WECKER_MAX - vergangen);
        f0 = frames_gesamt;
        if (!melodie_spielen(melodie, rest, 1)) {
            vergangen += (uint16_t)(frames_gesamt - f0);
            if (vergangen >= WECKER_MAX)
                break;
            continue;
        }
        vergangen += (uint16_t)(frames_gesamt - f0);
        if (raetsel == 0)
            break;
        f0 = frames_gesamt;
        if ((raetsel == 1 ? raetsel_folge() : raetsel_rechnen()))
            break;
        vergangen += (uint16_t)(frames_gesamt - f0);
        if (vergangen >= WECKER_MAX)
            break;
    }
    bild_bauen();
    /* Short message via the UART so that the ringing can be logged on the PC
     * without sitting next to it. The display resumes right away. */
    anzeige_aus();
    uart_init();
    uart_puts("wecker\r\n");
}

static void laufschrift(void)
{
    uint16_t off;
    uint8_t k, alt = uart_frei;

    uart_frei = 0;
    for (off = 0; off <= (uint16_t)(strom_len + 24); off++) {
        fenster(off);
        for (k = 0; k < SCHRITT_FRAMES; k++)
            rahmen();
    }
    anzeige_aus();
    uart_init();
    uart_frei = alt;
}

static void dez(uint16_t v)
{
    uint8_t z[5], i = 0;

    if (!v) {
        uart_putc('0');
        return;
    }
    while (v) {
        z[i++] = (uint8_t)('0' + v % 10);
        v /= 10;
    }
    while (i)
        uart_putc(z[--i]);
}

/* Commands from the remote side. In the halted state the characters come
 * directly from the UART (sent without gaps), in live operation from the
 * line that zeile_lesen() assembled under the protection of column 1. */
static uint8_t ein_avail(void)
{
    return live ? (uint8_t)(zeile_pos < zeile_len) : uart_avail();
}

static uint8_t ein_getc(void)
{
    if (!live)
        return uart_getc();
    return zeile_pos < zeile_len ? zeile[zeile_pos++] : (uint8_t)'\n';
}

#include "kalender.c"

static void befehlsschleife(void)
{
    for (;;) {
        uint8_t t, j;

        if (!ein_avail()) {
            if (live)
                return;
            continue;
        }
        t = ein_getc();
        if (t == 'R') {
            uart_puts("Neustart...\r\n");
            RESET_IN_BOOTLOADER();
        } else if (t == 'k') {
            /* test: light n rows per column ('k0'..'k7'), 'K' releases */
            uint8_t q = (uint8_t)(ein_getc() - '0'), n;

            test_halten = 1;
            for (n = 0; n < SPALTEN; n++)
                bild[n] = (uint8_t)((1u << q) - 1);
            uart_puts("ok\r\n");
        } else if (t == 'K') {
            test_halten = 0;
            uart_puts("ok\r\n");
        } else if (t == 'M') {
            /* Mn: play melody n (1-5), about 8 s, touching aborts */
            uint8_t q = (uint8_t)(ein_getc() - '1');

            if (q < MELODIEZAHL) {
                ton_spaet = 0;
                melodie_spielen(q, 1430, 1);
                uart_puts("gespielt, spaet ");
                dez(ton_spaet);
                uart_puts("\r\n");
            }
        } else if (t == 'N') {
            /* Nn: choose alarm melody n (1-5) */
            uint8_t q = (uint8_t)(ein_getc() - '1');

            if (q < MELODIEZAHL) {
                melodie = q;
                einst_speichern();
                uart_puts("melodie gewaehlt\r\n");
            }
        } else if (t == 'v') {
            /* light sensor (ADC channel 6) and current brightness level */
            dez(adc_lesen(6));
            uart_putc(' ');
            dez(helligkeit);
            uart_puts(" touch ");       /* idle/current circle, idle/current triangle */
            dez(ruhe_kreis); uart_putc('/'); dez(tastwert(3)); uart_putc(' ');
            dez(ruhe_dreieck); uart_putc('/'); dez(tastwert(4));
            uart_puts(" nacht ");       /* setting, dark, wake frames */
            dez(nacht_modus); uart_putc('/'); dez(nacht_dunkel); uart_putc('/'); dez(wach_rahmen);
            uart_puts(" weck mic/touch ");
            dez(weck_mic); uart_putc('/'); dez(weck_touch);
            uart_puts(" spitze ");
            dez(mic_spitze);
            mic_spitze = 0;
            uart_puts("\r\n");
        } else if (t == 'H') {
            /* H1..H8 fixed brightness, H0 back to light sensor */
            uint8_t q = (uint8_t)(ein_getc() - '0');

            if (q <= 8) {
                hell_fest = q;
                if (q) {
                    helligkeit = q;
                    helligkeit_uebernehmen();
                }
                uart_puts("hell ");
                dez(q);
                uart_puts("\r\n");
            }
        } else if (t == 'O') {
            /* O1 light organ on, O0 off, O2 trigger one wave (for testing) */
            uint8_t q = (uint8_t)(ein_getc() - '0');

            if (q == 1) {
                wecken();
                orgel_start();
            } else if (q == 0 && orgel_aktiv) {
                orgel_ende();
            } else if (q == 2 && orgel_aktiv) {
                orgel_welle();
            }
            uart_puts("orgel ");
            dez(orgel_aktiv);
            uart_puts("\r\n");
        } else if (t == 'Q') {
            /* Q0/Q1: night mode off/on; Q2: display off immediately, until a clap
             * or touch */
            uint8_t q = (uint8_t)(ein_getc() - '0');

            if (q == 2) {
                aus_manuell = 1;
                wach_rahmen = 0;
                nacht_dunkel = 1;
                uart_puts("aus\r\n");
            } else if (q <= 1) {
                nacht_modus = q;
                einst_speichern();
                if (!q)
                    wecken();
                uart_puts("nacht ");
                dez(q);
                uart_puts("\r\n");
            }
        } else if (t == 'U') {
            /* U0/U1/U2: alarm puzzle off/sequence/arithmetic; U9: test alarm */
            uint8_t q = (uint8_t)(ein_getc() - '0');

            if (q <= 2) {
                raetsel = q;
                einst_speichern();
            } else if (q == 9) {
                alarm_laeuten();
            }
            uart_puts("quiz ");
            dez(raetsel);
            uart_puts("\r\n");
        } else if (t == 'Y') {
            kal_befehl();
        } else if (t == 'C') {
            /* CMMSS: start countdown, C0000 ends it */
            uint8_t d4[4];

            for (j = 0; j < 4; j++)
                d4[j] = (uint8_t)(ein_getc() - '0');
            countdown_start((uint16_t)((d4[0] * 10 + d4[1]) * 60u + d4[2] * 10 + d4[3]));
            uart_puts("countdown ");
            dez(count_rest);
            uart_puts("\r\n");
        } else if (t == 'V') {
            /* Vxn: set a display setting. x = w transition 0-8, z digit effect
             * 0-5, d colon 1-5, i info 0-5 (5/10/30/60/120 s, 5 = off),
             * t theme 1-3 */
            uint8_t a = ein_getc(), q = (uint8_t)(ein_getc() - '0');

            if (a == 'w' && q <= 8) wechsel = q;
            else if (a == 'z' && q <= 5) ziffer_eff = q;
            else if (a == 'd' && q >= 1 && q <= 5) dpunkt = q;
            else if (a == 'i' && q <= 5) info_int = q;
            else if (a == 't' && q >= 1 && q <= 3) theme = q;
            else if (a == 'l' && q <= 3) led_eff = q;
            else if (a == 'h' && q <= 8) hell_fest = q;
            else if (a == 's' && q >= 1 && q <= 3) schrift = q;
            else if (a == 'm' && q >= MIKRO_MIN && q <= MIKRO_MAX) mikro = q;
            einst_speichern();
            uart_puts("ok\r\n");
        } else if (t == 'T') {
            /* temperature of the DS3231 (measured on the chip), e.g. "21.25" */
            uint8_t roh[2];

            if (rtc_lesen(0x11, roh, 2)) {
                dez(roh[0]);
                uart_putc('.');
                j = (uint8_t)((roh[1] >> 6) * 25);
                uart_putc((uint8_t)('0' + j / 10));
                uart_putc((uint8_t)('0' + j % 10));
            }
            uart_puts("\r\n");
        } else if (t == 'E') {
            /* settings in EEPROM (sector 0, first 8 bytes) */
            uint8_t a, v;

            for (a = 0; a < E_ZAHL; a++) {
                v = iap_lesen(a);
                uart_putc("0123456789ABCDEF"[v >> 4]);
                uart_putc("0123456789ABCDEF"[v & 15]);
                uart_putc(' ');
            }
            uart_puts("\r\n");
        } else if (t == 'x') {
            /* characters, syncs, executed lines, checksum errors, dark time slots */
            dez(rx_n); uart_putc(' '); dez(rx_sync); uart_putc(' ');
            dez(rx_zeilen); uart_putc(' '); dez(rx_fehler); uart_putc(' ');
            dez(dunkel_slots); uart_puts("\r\n");
        } else if (t == 'h') {
            /* Halt: display off, then the loop reads the UART directly
             * until 'w' arrives. */
            live = 0;
            tx_puffern = 0;             /* send directly from here on */
            anzeige_aus();
            uart_init();
            uart_puts("angehalten. 'd' Register, 'sHHMMSS' stellen, "
                      "'w' weiter, 'R' Bootloader\r\n");
        } else if (t == 'w') {
            uart_puts("weiter\r\n");
            return;
        } else if (t == 'u') {
            tx_puffern = 0;
            uart_puts("update\r\n");
            update_modus();
        } else if (t == 'a') {
            /* aHHMMx: set alarm, x = 1 on, 0 off. For testing
             * without keys. */
            uint8_t va[2], an2, zz, ee;

            for (j = 0; j < 2; j++) {
                zz = (uint8_t)(ein_getc() - '0');
                ee = (uint8_t)(ein_getc() - '0');
                va[j] = (uint8_t)(zz * 10 + ee);
            }
            an2 = (uint8_t)(ein_getc() == '1');
            alarm_schreiben(an2, va[0], va[1]);
            uart_puts("alarm gesetzt\r\n");
        } else if (t == 'r') {
            /* registers 0B (theme), 0E (control), 0F (status) */
            uint8_t ad, v;

            for (j = 0; j < 3; j++) {
                ad = (uint8_t)(j == 0 ? 0x0B : (j == 1 ? 0x0E : 0x0F));
                v = 0;
                rtc_lesen(ad, &v, 1);
                uart_puts(j == 0 ? "0B=" : (j == 1 ? " 0E=" : " 0F="));
                uart_putc("0123456789ABCDEF"[v >> 4]);
                uart_putc("0123456789ABCDEF"[v & 0x0F]);
            }
            uart_puts("\r\n");
        } else if (t == 'l' || t == 'L') {
            /* 'lText\n' runs as scrolling text, 'LText\n' only
             * loads it and reports the length of the column stream. */
            uint16_t n = 0;
            uint8_t q;

            for (;;) {
                q = ein_getc();
                if (q == '\n' || q == '\r')
                    break;
                if (n < TEXT_MAX)
                    textpuffer[n++] = q;
            }
            text_laden(n);
            if (t == 'L') {
                uart_puts("ok ");
                dez(strom_len);
                uart_puts("\r\n");
            } else if (live) {
                /* In operation the scrolling text runs in the main loop;
                 * the clock keeps accepting commands meanwhile. */
                lauf_off = 0;
                lauf_k = 0;
                lauf_aktiv = 1;
                wecken();
                uart_puts("ok\r\n");
            } else {
                laufschrift();
                uart_puts("fertig\r\n");
            }
        } else if (t == 'S') {
            /* column stream as hex - for checking on the PC */
            uint16_t m;

            for (m = 0; m < strom_len; m++) {
                uart_putc("0123456789ABCDEF"[strom[m] >> 4]);
                uart_putc("0123456789ABCDEF"[strom[m] & 0x0F]);
            }
            uart_puts("\r\n");
        } else if (t == 'W') {
            /* WNNNN: window at step NNNN into the frame buffer */
            uint16_t off = 0;
            uint8_t q2;

            for (j = 0; j < 4; j++) {
                q2 = (uint8_t)(ein_getc() - '0');
                off = (uint16_t)(off * 10 + q2);
            }
            fenster(off);
            uart_puts("ok\r\n");
        } else if (t == 'z') {
            /* zHHMM: put theme 2 with arbitrary digits into the frame
             * buffer without changing the time.
             * Used to check the glyphs against the photo. */
            uint8_t d4[4];

            for (j = 0; j < 4; j++)
                d4[j] = (uint8_t)(ein_getc() - '0');
            anzeige_t2((uint8_t)(d4[0] * 10 + d4[1]),
                       (uint8_t)(d4[2] * 10 + d4[3]), DOPPELPUNKT);
            uart_puts("ok\r\n");
        } else if (t == 'b') {
            /* frame buffer in hex - this lets the layout be checked
             * without a camera pointed at the display. */
            for (j = 0; j < SPALTEN; j++) {
                uart_putc("0123456789ABCDEF"[bild[j] >> 4]);
                uart_putc("0123456789ABCDEF"[bild[j] & 0x0F]);
            }
            uart_puts("\r\n");
        } else if (t == 't') {
            t = ein_getc();
            theme = (uint8_t)(t == '2' ? 2 : 1);
            theme_speichern();
            bild_bauen();
            uart_puts("ok\r\n");
        } else if (t == 'd') {
            if (rtc_lesen(0x00, zeit, 7)) {
                for (j = 0; j < 7; j++) {
                    uart_putc("0123456789ABCDEF"[zeit[j] >> 4]);
                    uart_putc("0123456789ABCDEF"[zeit[j] & 0x0F]);
                    uart_putc(' ');
                }
            } else {
                uart_puts("keine Antwort");
            }
            uart_puts("\r\n");
        } else if (t == 's') {
            uint8_t v[3];

            for (j = 0; j < 3; j++) {
                uint8_t zz = (uint8_t)(ein_getc() - '0');
                uint8_t e = (uint8_t)(ein_getc() - '0');
                v[j] = (uint8_t)(zz * 10 + e);
            }
            rtc_schreiben(0x00, zubcd(v[2]));
            rtc_schreiben(0x01, zubcd(v[1]));
            rtc_schreiben(0x02, zubcd(v[0]));
            uart_puts("gestellt\r\n");
        } else if (t == 'D') {
            /* DTTMMJJW: set date, W = weekday 1 (Monday) to 7 */
            uint8_t v[3], w;

            for (j = 0; j < 3; j++) {
                uint8_t zz = (uint8_t)(ein_getc() - '0');
                uint8_t e = (uint8_t)(ein_getc() - '0');
                v[j] = (uint8_t)(zz * 10 + e);
            }
            w = (uint8_t)(ein_getc() - '0');
            if (v[0] >= 1 && v[0] <= 31 && v[1] >= 1 && v[1] <= 12 && w >= 1 && w <= 7) {
                rtc_schreiben(0x03, w);
                rtc_schreiben(0x04, zubcd(v[0]));
                rtc_schreiben(0x05, zubcd(v[1]));   /* bit 7 (century) stays 0 */
                rtc_schreiben(0x06, zubcd(v[2]));
                bild_bauen();
                uart_puts("datum gestellt\r\n");
            } else {
                uart_puts("datum ungueltig\r\n");
            }
        }
    }
}

/* Lines that the UART interrupt accepted in a pause. 0xFF in the buffer
 * marks the start of the line, '\n' the end. Before the end of line there is
 * a check character: 0x40 + (sum of the characters & 0x3F). A line missing
 * bytes must never be executed - a truncated "hallo" would otherwise be
 * 'h' (halt), 'a' (alarm) and so on. During execution the matrix stays dark
 * so that the reply goes out cleanly via TxD (column 2); that takes a few
 * milliseconds. */
/* Send the collected reply: with the display lit, wait for a dark time slot
 * first, because TxD is column 2. */
static void antwort_senden(void)
{
    uint8_t n;

    if (tx_len && aktiv) {
        ausfuehrung = 1;
#ifndef ZWILLING
        while (!slot_dunkel)            /* current time slot finished */
            ;
#endif
        EA = 0;
        P3M1 &= (uint8_t)~0x02;         /* TxD push-pull */
        P3M0 |= 0x02;
        EA = 1;
        for (n = 0; n < tx_len; n++)
            uart_putc(tx_buf[n]);
        ausfuehrung = 0;
    } else {
        for (n = 0; n < tx_len; n++)    /* display off (e.g. after 'h') */
            uart_putc(tx_buf[n]);
    }
}

static void live_empfang(void)
{
    uint8_t sum, n, ok;

    zeile_da = 0;
#ifdef ZWILLING
    /* the twin has no UART interrupt: assemble the line as it would */
    while (!zeile_fertig && uart_avail()) {
        uint8_t c = uart_getc();
        if (c == 0xFF) {
            zeile_auf = 1;
            zeile_len = 0;
            zeile_lang = 0;
        } else if (!zeile_auf) {
        } else if (c == '\n' || c == '\r') {
            zeile_auf = 0;
            zeile_fertig = 1;
        } else if (zeile_len < sizeof zeile) {
            zeile[zeile_len++] = c;
        } else {
            zeile_lang = 1;
        }
    }
#endif
    if (!zeile_fertig)
        return;
    halten = 0;
    tx_len = 0;
    tx_puffern = 1;
    sum = 0;
    for (n = 0; (uint8_t)(n + 1) < zeile_len; n++)
        sum += zeile[n];
    ok = (uint8_t)(zeile_len >= 2 && zeile[zeile_len - 1] == (uint8_t)(0x40 + (sum & 0x3F)));
    if (zeile_lang) {
        /* Too long: say so, so that the sender does not just repeat it. */
        rx_fehler++;
        uart_puts("zu lang\r\n");
    } else if (!ok) {
        /* Bytes missing or garbled: never execute, the sender repeats. */
        rx_fehler++;
        uart_puts("pruef\r\n");
    } else {
        /* Execute with the display lit, only collect the reply. It only goes
         * dark again for sending: TxD is column 2. */
        zeile_len--;
        live = 1;
        rx_zeilen++;
        zeile_pos = 0;
        befehlsschleife();
        live = 0;
    }
    tx_puffern = 0;
    antwort_senden();
    zeile_fertig = 0;                   /* the interrupt may take the next line */
}

/* Copy the initial values of initialised XRAM variables ourselves.
 *
 * SDCC's startup routine (crtxinit) copies them with MOVX @R0 and uses P2 as
 * the high address byte - the trick of the original 8051. The STC8 does not
 * support that: the values never arrived. After a cold start they contained
 * random values, after a start from the bootloader the leftovers of the
 * previous firmware. Measured: "halten" was 1 without any sync, and the
 * display stayed dark.
 *
 * This hook runs before crtxinit. It copies via DPTR, which the STC8 does
 * correctly. Whatever crtxinit then writes to the wrong place is cleared
 * again afterwards by crtxclear (it clears the remaining XRAM via DPTR). */
#ifndef ZWILLING
unsigned char __sdcc_external_startup(void) __naked
{
    __asm
        mov     r1,#l_XINIT
        mov     a,r1
        orl     a,#(l_XINIT >> 8)
        jz      09903$
        mov     r2,#((l_XINIT + 255) >> 8)
        mov     r4,#s_XINIT
        mov     r5,#(s_XINIT >> 8)
        mov     r6,#s_XISEG
        mov     r7,#(s_XISEG >> 8)
09901$:
        mov     dpl,r4
        mov     dph,r5
        clr     a
        movc    a,@a+dptr
        inc     dptr
        mov     r4,dpl
        mov     r5,dph
        mov     dpl,r6
        mov     dph,r7
        movx    @dptr,a
        inc     dptr
        mov     r6,dpl
        mov     r7,dph
        djnz    r1,09901$
        djnz    r2,09901$
09903$:
        mov     dpl,#0x00               ; 0: normale Initialisierung danach
        ret
    __endasm;
}
#endif

/* rear-LED effects, every frame */
static uint8_t led_blitz;
static uint16_t led_phase;

static void led_tick(void)
{
    uint16_t p;

    /* Light organ: rear LEDs dark. Lit, they raise the microphone so far
     * that a wave triggers the next one (measured 2026-11-14). */
    switch (orgel_aktiv ? 0 : led_eff) {
    case 1:
        led_pegel = 64;                     /* constant: full (chosen by the user) */
        break;
    case 2:                                 /* decays softly in about 0.2 s */
        led_pegel = led_blitz;
        led_blitz = (uint8_t)(led_blitz > 2 ? led_blitz - 2 : 0);
        break;
    case 3:                                 /* breathing: about 3 s, quadratic - the eye */
        if (++led_phase >= 534)             /* needs fine steps at the bottom */
            led_phase = 0;
        p = led_phase < 267 ? led_phase : (uint16_t)(534 - led_phase);
        p = (uint16_t)(p * 64u / 267u);     /* 0..64 */
        led_pegel = (uint8_t)(p * p / 64u);
        break;
    default:
        led_pegel = 0;
        break;
    }
#ifdef ZWILLING
    zw_led(led_pegel);
#endif
}

void main(void)
{
    uint8_t k, d;
    uint16_t z = 0, beide = 0;
    uint8_t faellig = 0, n;

    zb = bild;
    anzeige_aus();
    uart_init();
    IP |= 0x10;                 /* UART before timer 0: at a high rate only a
                                 * few us remain between two characters */
    ES = 1;
    adc_init();
    /* The alarm enable lives in the clock's control register and must be read
     * BEFORE the first buzzer() call. It used to live only in RAM with the
     * initial value 0x04, and buzzer(0) wrote this value back immediately -
     * so the alarm was off again after every restart. */
    {
        uint8_t ctl = 0, a = 0, mel = 0;

        rtc_lesen(0x0E, &ctl, 1);
        rtc_lesen(0x0A, &a, 1);
        /* The enable now lives in 0x0A bit 0; older firmware had it in
         * A1IE (0x0E bit 0) - take it over once. */
        alarm_an = (uint8_t)(((a & 0x81) == 0x81) || (ctl & 0x01));
        if ((ctl & 0x01) && (a & 0x80))
            rtc_schreiben(0x0A, (uint8_t)(a | 0x01));
        if (rtc_lesen(0x0C, &mel, 1) && mel < MELODIEZAHL)
            melodie = mel;
        if (rtc_lesen(0x0D, &mel, 1) && mel == 1)
            nacht_modus = 1;
    }
    buzzer(0);                  /* writes 0x0E: buzzer off, A1IE off */
    touch_einmessen();
    {
        uint8_t tv;

        if (rtc_lesen(0x0B, &tv, 1) && tv == 2)
            theme = 2;
        if (!einst_laden())     /* first start: take over the values from the DS3231 */
            einst_speichern();
    }
    uart_puts("\r\nSTC8 UHR2\r\n"
              "Dreieck kurz: Zeit / Datum / Temperatur\r\n"
              "Kreis kurz:   Rueck-LEDs\r\n"
              "beide 2 s:    Menue - ZEIT DATUM ALARM TON LED THEMA ENDE\r\n"
              "              darin Dreieck weiter, Kreis waehlen\r\n"
              "Befehle: 0xFF-Folge, Zeile, Pruefzeichen, Zeilenende\r\n"
              "(host/live.py); 'h' haelt an, 'w' weiter, 'R' Bootloader\r\n");
    /* Start-up diagnostics: without them one cannot tell whether the display
     * is too dark or simply has no content. */
    {
        uint8_t j, n = 0;

        uart_puts("RTC ");
        if (rtc_lesen(0x00, zeit, 7)) {
            for (j = 0; j < 3; j++) {
                uart_putc((uint8_t)('0' + (bcd(zeit[2 - j]) / 10)));
                uart_putc((uint8_t)('0' + (bcd(zeit[2 - j]) % 10)));
                if (j < 2) uart_putc(':');
            }
        } else {
            uart_puts("antwortet nicht");
        }
        uart_puts("   Licht ");
        uart_putc((uint8_t)('0' + (adc_lesen(6) / 1000) % 10));
        uart_putc((uint8_t)('0' + (adc_lesen(6) / 100) % 10));
        uart_puts("xx   Stufe ");
        uart_putc((uint8_t)('0' + helligkeit));
        uart_puts("\r\nTouch ");
        uart_putc((uint8_t)('0' + (ruhe_kreis / 1000) % 10));
        uart_putc((uint8_t)('0' + (ruhe_kreis / 100) % 10));
        uart_putc('/');
        uart_putc((uint8_t)('0' + (ruhe_dreieck / 1000) % 10));
        uart_putc((uint8_t)('0' + (ruhe_dreieck / 100) % 10));
        bild_bauen();
        for (j = 0; j < SPALTEN; j++)
            if (bild[j]) n++;
        uart_puts("   belegte Spalten ");
        uart_putc((uint8_t)('0' + n / 10));
        uart_putc((uint8_t)('0' + n % 10));
        uart_puts("\r\n");
    }

    for (;;) {
        if (orgel_aktiv) {
            n = klatsch_rahmen(ORGEL_SCHWELLE);
            if (orgel_tot) {                /* dead time after a wave: do not listen */
                orgel_tot--;
                laut = 0;
            } else {
                laut = (uint8_t)(laut + n);
            }
            if (laut >= MIC_TREFFER) {
                orgel_welle();
                laut = 0;
            } else if (++laut_fenster >= 10) {
                laut_fenster = 0;
                laut = 0;
            }
            if (++orgel_k >= ORGEL_SCHRITT) {
                orgel_k = 0;
                orgel_schritt();
            }
        } else if (nacht_dunkel) {
            laut = (uint8_t)(laut + klatsch_rahmen(MIC_SCHWELLE));
            if (laut >= MIC_TREFFER) {
                weck_mic++;
                wecken();
            }
            else if (++laut_fenster >= 10) {
                laut_fenster = 0;
                laut = 0;
            }
        } else if (led_eff == 2) {          /* rear LEDs follow the sound: every */
            /* The threshold grows with the clock's own level: lit rear LEDs couple
             * into the microphone and otherwise triggered flashes without a
             * noise. Fewer than three loud values per frame do not count. */
            n = klatsch_rahmen((uint16_t)(ORGEL_SCHWELLE + (led_pegel >> 1)));
            n = (uint8_t)(n < 3 ? 0 : (n > 8 ? 64 : n * 8));
            led_blitz = (uint8_t)(led_blitz + n > 64 ? 64 : led_blitz + n);
        } else {
            rahmen_oder_zeile();
        }
        led_tick();
        if (minute_neu) {                   /* appointments take priority, even over */
            minute_neu = 0;                 /* a running all-day scrolling text */
            if (!orgel_aktiv)
                kal_termine();
        }
        if (count_ende) {                   /* countdown expired */
            count_ende = 0;
            count_aktiv = 0;
            wecken();
            melodie_spielen(melodie, 5350, 1);  /* about 30 s, touching ends it */
            schirm = S_ZEIT;
            bild_bauen();
        }
        if (wach_rahmen && --wach_rahmen == 0 && nacht_modus && !aus_manuell)
            nacht_dunkel = 1;   /* 15 s over; if it is bright, nacht_pruefen switches back on */
        live_empfang();
        uhr_tick();
        if (!lauf_aktiv && !orgel_aktiv && !test_halten)
            anzeige_tick();
        else if (dp_s1 != 0xFF)
            dp_dimmen(0xFF, 0xFF, 8);       /* special PWM only in the clock display */
        z++;


        if (lauf_aktiv && ++lauf_k >= SCHRITT_FRAMES) {
            lauf_k = 0;
            if (lauf_off > (uint16_t)(strom_len + 24)) {
                lauf_aktiv = 0;
                if (schirm == S_KAL && !kal_ganztags_weiter())
                    schirm = S_ZEIT;            /* no more entries today */
                bild_bauen();
            } else {
                fenster(lauf_off++);
            }
        }

        if (z % 20 == 0 && !halten) {           /* poll keys; not while holding
                                                 * (briefly disables EA) */
            touch_lesen(&k, &d);
            if (count_aktiv && k && !war_kreis) {
                countdown_start(0);             /* circle aborts the countdown */
                piep();
            } else if (orgel_aktiv && (k || d)) {
                orgel_ende();                   /* touching ends the light organ */
            } else if (nacht_dunkel && (k || d)) {
                weck_touch++;
                wecken();                       /* touching only wakes up */
            } else if (lauf_aktiv && (k || d)) {
                lauf_aktiv = 0;                 /* touching aborts the scrolling text */
                bild_bauen();
            } else if (k && d) {
                /* Hold for about two seconds (30 polls every 20 frames at
                 * 3.1 ms per frame, calculated; with 60 it was about four,
                 * and the user reported "a bit longer than 2 s"). With half a
                 * second a false trigger of the keys started the setting mode
                 * and silently changed the time - the hour was then 02
                 * instead of 12. */
                if (++beide > 19) { menue(); beide = 0; bild_bauen(); }
            } else {
                beide = 0;
                if (d && !war_dreieck) {
                    anzeige_weiter();           /* next screen, with animation */
                    piep();
                }
                if (k && !war_kreis) {
                    if (led_eff) {              /* circle: rear LEDs off/on */
                        led_merk = led_eff;
                        led_eff = 0;
                    } else {
                        led_eff = led_merk;
                    }
                    einst_speichern();
                    piep();
                }
            }
            war_kreis = k;
            war_dreieck = d;
        }

        if (z % 100 == 0)
            faellig = 1;
        if (faellig && !halten) {       /* not while a line is coming in */
            faellig = 0;
            helligkeit_regeln();
            nacht_pruefen();
            if (alarm_faellig())
                alarm_laeuten();
        }

    }
}
