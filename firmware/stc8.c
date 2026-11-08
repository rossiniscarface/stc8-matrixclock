#include "stc8.h"

#ifdef UART_RX_IRQ
extern volatile __xdata uint8_t rx_buf[64];
extern volatile uint8_t rx_w, rx_r;
/* Reply buffer: while tx_puffern is set, uart_putc only collects the
 * characters. The clock executes commands with the display lit and only goes
 * dark briefly for sending (TxD is also matrix column 2). */
extern volatile uint8_t tx_puffern, tx_len;
extern __xdata uint8_t tx_buf[];
#define TX_MAX 128
__sbit __at (0xAC) ES;
#endif

void uart_init(void)
{
    AUXR |= 0x04;              /* T2x12: timer 2 in 1T mode */
    T2L = (uint8_t)(T2_RELOAD & 0xFF);
    T2H = (uint8_t)(T2_RELOAD >> 8);
    AUXR |= 0x10;              /* T2R: start timer 2 */
    AUXR |= 0x01;              /* S1ST2: UART1 uses timer 2 as clock source */
    SCON  = 0x50;              /* mode 1, 8 bit, receive enabled */
    TI = 0;
    RI = 0;
#ifdef UART_RX_IRQ
    rx_r = rx_w;
#endif
}

void uart_putc(uint8_t c)
{
#ifdef UART_RX_IRQ
    __bit es;

    if (tx_puffern) {
        if (tx_len < TX_MAX)
            tx_buf[tx_len++] = c;
        return;
    }
    es = ES;
    ES = 0;
#endif
    SBUF = c;
    while (!TI)
        ;
    TI = 0;
#ifdef UART_RX_IRQ
    ES = es;
#endif
}

void uart_puts(const char *s)
{
    while (*s)
        uart_putc((uint8_t)*s++);
}

void uart_puthex8(uint8_t v)
{
    const char *hex = "0123456789ABCDEF";
    uart_putc((uint8_t)hex[v >> 4]);
    uart_putc((uint8_t)hex[v & 0x0F]);
}

#ifdef UART_RX_IRQ
uint8_t uart_avail(void)
{
    return rx_w != rx_r;
}

uint8_t uart_getc(void)
{
    uint8_t c;
    while (rx_w == rx_r)
        ;
    c = rx_buf[rx_r & 63];
    rx_r++;
    return c;
}
#else
uint8_t uart_avail(void)
{
    return RI ? 1 : 0;
}

uint8_t uart_getc(void)
{
    uint8_t c;
    while (!RI)
        ;
    c = SBUF;
    RI = 0;
    return c;
}
#endif

/*
 * Switch all port pins to high impedance — except P3.0/P3.1, which stay
 * quasi-bidirectional, otherwise the UART fails.
 *
 * This is the safe default state: the matrix LEDs have no series
 * resistors, and Q1 at the buzzer conducts on low. High impedance drives
 * none of them.
 */
void ports_all_highz(void)
{
    /* Latches to 1 first. In quasi-bidirectional mode a 0 in the latch drives
     * the pin hard low — and that is exactly what the first failed attempt
     * looked like: TxD permanently low, an endless stream of 0x00 at the
     * receiver. */
    P3 |= 0x03;

    P0M1 = 0xFF; P0M0 = 0x00;
    P1M1 = 0xFF; P1M0 = 0x00;
    P2M1 = 0xFF; P2M0 = 0x00;
    /* P3.1 (TxD) push-pull, P3.0 (RxD) high-impedance input.
     * Quasi-bidirectional drives high strongly for only two clocks and then
     * holds with a weak pull-up - against the cable capacitance the edge
     * then rises slower than one bit time, and every single 1 between zeros
     * gets lost. That was also exactly why 115200 never worked. */
    P3M1 = 0xFD; P3M0 = 0x02;
    P4M1 = 0xFF; P4M0 = 0x00;
    P5M1 = 0xFF; P5M0 = 0x00;
}
