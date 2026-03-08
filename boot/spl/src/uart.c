/* ============================================================
 * boot/spl/src/uart.c
 * Lightweight UART0 Driver for AM335x SPL
 *
 * Minimal polling-only console for early SPL output.
 * ============================================================ */

#include "am335x.h"
#include "boot.h"

/* Basic line configuration values. */
#define UART_LCR_8N1            0x03U   /* 8 data bits, no parity, 1 stop  */
#define UART_LCR_DLAB_ENABLE    0x83U   /* Divisor Latch Access + 8N1      */

/* 115200 baud @ 48MHz functional clock. */
#define UART_DIVISOR            26U

void uart_flush(void)
{
    /* Wait until the transmitter is fully idle. */
    while ((readl(UART0_LSR) & UART_LSR_TEMT) == 0);
}

void uart_init(void)
{
    /* Enable UART clock before touching the block. */
    writel(CLKCTRL_MODULEMODE_ENABLE, CM_WKUP_UART0_CLKCTRL);
    delay(10000);

    /* Leave operational mode before reconfiguration. */
    writel(UART_DISABLE, UART0_MDR1);
    delay(1000);

    /* Clear stale FIFO state from earlier boot stages. */
    writel(0x07U, UART0_FCR);
    delay(1000);

    /* Enter divisor-programming mode. */
    writel(UART_LCR_DLAB_ENABLE, UART0_LCR);

    /* Program baud divisor. */
    writel(UART_DIVISOR & 0xFFU,         UART0_DLL);
    writel((UART_DIVISOR >> 8) & 0xFFU,  UART0_DLH);

    /* Return to normal 8N1 line format. */
    writel(UART_LCR_8N1, UART0_LCR);

    /* SPL uses polling only. */
    writel(0x00U, UART0_IER);

    /* Re-enter normal UART mode. */
    writel(UART_16X_MODE, UART0_MDR1);

    /* Give the block a moment to settle. */
    delay(10000);

    /* Drain startup garbage before first print. */
    uart_flush();
}

void uart_putc(char c)
{
    /* Keep terminal output readable on CRLF consoles. */
    if (c == '\n') {
        uart_putc('\r');
    }

    /* Wait for transmit space. */
    while ((readl(UART0_LSR) & UART_LSR_TXFIFOE) == 0);

    /* Push one byte. */
    writeb((uint8_t)c, UART0_THR);

    /* Small guard delay for the next status read. */
    { volatile uint32_t d; for (d = 0; d < 100; d++); }

    /* Wait until the byte has left the holding path. */
    while ((readl(UART0_LSR) & UART_LSR_TXFIFOE) == 0);
}

void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

void uart_print_hex(uint32_t val)
{
    static const char digits[] = "0123456789ABCDEF";
    int i;

    uart_puts("0x");
    for (i = 28; i >= 0; i -= 4) {
        uart_putc(digits[(val >> i) & 0xFU]);
    }
}
