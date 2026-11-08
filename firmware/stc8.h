/*
 * SFR definitions for the STC8A4K32S2A12 (LQFP44), only what is needed here.
 * Port mode: PxM1/PxM0 = 00 quasi-bidirectional, 01 push-pull,
 *            10 high-impedance input, 11 open-drain.
 */
#ifndef STC8_H
#define STC8_H

#include <stdint.h>

__sfr __at (0x80) P0;
__sfr __at (0x90) P1;
__sfr __at (0xA0) P2;
__sfr __at (0xB0) P3;
__sfr __at (0xC0) P4;
__sfr __at (0xC8) P5;

__sfr __at (0x93) P0M1;
__sfr __at (0x94) P0M0;
__sfr __at (0x91) P1M1;
__sfr __at (0x92) P1M0;
__sfr __at (0x95) P2M1;
__sfr __at (0x96) P2M0;
__sfr __at (0xB1) P3M1;
__sfr __at (0xB2) P3M0;
__sfr __at (0xB3) P4M1;
__sfr __at (0xB4) P4M0;
__sfr __at (0xC9) P5M1;
__sfr __at (0xCA) P5M0;

__sfr __at (0x8E) AUXR;
__sfr __at (0xC7) IAP_CONTR;
__sfr __at (0x98) SCON;
__sfr __at (0x99) SBUF;
__sfr __at (0xD6) T2H;
__sfr __at (0xD7) T2L;

__sbit __at (0x98) RI;
__sbit __at (0x99) TI;

/* Baud rate reload for timer 2 in 1T mode: 65536 - FOSC / (4 * baud).
 *
 * 57600 instead of 115200: the probe baudprobe.c measured that 9600 to 57600
 * are exact (factor 1.0000), but from 115200 on nothing readable arrives.
 * Cause found (2026-11-07): TxD was quasi-bidirectional, where only a weak
 * pull-up holds the high edge. Against the cable capacitance it rose slower
 * than one bit time, and every single 1 between zeros got lost — always at
 * 115200, eventually at 57600 too. stcgal worked because the bootloader
 * drives push-pull. Since ports_all_highz() sets P3.1 to push-pull, the edge
 * is no longer an issue; 57600 stays with plenty of margin. */
#define FOSC        18432000UL
#ifndef BAUD
#define BAUD        57600UL
#endif
#define T2_RELOAD   (65536UL - FOSC / (4UL * BAUD))

void uart_init(void);
void uart_putc(uint8_t c);
void uart_puts(const char *s);
void uart_puthex8(uint8_t v);
uint8_t uart_getc(void);
uint8_t uart_avail(void);
void ports_all_highz(void);

/*
 * Software reset into the bootloader.
 *
 * IAP_CONTR: bit 6 = SWBS (where to boot), bit 5 = SWRST (triggers).
 * 0x60 sets both -> restart into the ISP monitor, 0x20 would restart into
 * the application. This removes the manual power cycle before every flash.
 */
#define RESET_IN_BOOTLOADER()   do { IAP_CONTR = 0x60; } while (0)

#endif
