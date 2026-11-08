/* Digital twin: uhr2.c is compiled with gcc for the PC.
 *
 * This file lets gcc understand the SDCC keywords (special function
 * registers become ordinary variables) and declares the replacement
 * functions that uhr2.c hands over to at the hardware spots
 * (#ifdef ZWILLING). The firmware logic itself - menu, display, alarm,
 * animations - runs unchanged. */
#ifndef ZWILLING_H
#define ZWILLING_H

#include <stdint.h>

#define __sfr          volatile unsigned char
#define __sbit         volatile unsigned char
#define __at(x)
#define __code
#define __xdata
#define __data
#define __idata
#define __bit          unsigned char
#define __interrupt(x)
#define __using(x)
#define __naked

void zw_rahmen(const uint8_t *bild);
uint8_t zw_rtc_lesen(uint8_t ab, uint8_t *ziel, uint8_t n);
uint8_t zw_rtc_schreiben(uint8_t ab, uint8_t wert);
uint16_t zw_adc(uint8_t kanal);
uint8_t zw_mic(uint16_t schwelle);
uint16_t zw_tastwert(uint8_t b);
void zw_ton_name(const char *name);
void zw_led(uint8_t pegel);
void zw_hell(uint8_t stufe);
extern uint8_t zw_iap[32768];

#endif
