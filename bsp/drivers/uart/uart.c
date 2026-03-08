/* ===========================================================
 * bsp/drivers/uart/uart.c — UART driver
 *
 * Supports two hardware backends selected at compile time:
 *   PLATFORM_QEMU → PL011 UART (ARM PrimeCell UART)
 *   PLATFORM_BBB  → NS16550 compatible (AM335x UART0)
 *
 * Platform-specific base addresses come from bsp/include/board.h.
 * =========================================================== */

#include <stdint.h>
#include "../../include/board.h"
#include "uart.h"

/* ----------------------------------------------------------------
 * Helper macros: volatile register read/write
 * ---------------------------------------------------------------- */
#define REG32(addr)         (*((volatile uint32_t *)(addr)))
#define REG8(addr)          (*((volatile uint8_t  *)(addr)))

/* ================================================================
 * PLATFORM_QEMU — ARM PL011 UART
 *
 * Base: UART0_BASE = 0x10009000U (realview-pb-a8)
 * ================================================================ */
#ifdef PLATFORM_QEMU

/* PL011 register offsets */
#define PL011_DR        0x000U  /* Data Register (TX/RX FIFO)           */
#define PL011_FR        0x018U  /* Flag Register                         */
#define PL011_IBRD      0x024U  /* Integer Baud Rate Divisor             */
#define PL011_FBRD      0x028U  /* Fractional Baud Rate Divisor          */
#define PL011_LCR_H     0x02CU  /* Line Control Register                 */
#define PL011_CR        0x030U  /* Control Register                      */
#define PL011_IMSC      0x038U  /* Interrupt Mask Set/Clear              */

/* Flag Register bits */
#define PL011_FR_TXFF   (1U << 5)   /* TX FIFO Full  */
#define PL011_FR_RXFE   (1U << 4)   /* RX FIFO Empty */
#define PL011_FR_BUSY   (1U << 3)   /* UART Busy     */

/* LCR_H bits */
#define PL011_LCR_WLEN8 (0x3U << 5) /* 8-bit word length */
#define PL011_LCR_FEN   (1U << 4)   /* FIFO Enable       */

/* CR bits */
#define PL011_CR_UARTEN (1U << 0)   /* UART Enable  */
#define PL011_CR_TXE    (1U << 8)   /* TX Enable    */
#define PL011_CR_RXE    (1U << 9)   /* RX Enable    */

void uart_init(void)
{
    /* Disable UART before configuration */
    REG32(UART0_BASE + PL011_CR) = 0;

    /* Program baud divisor. */
    REG32(UART0_BASE + PL011_IBRD) = 13U;
    REG32(UART0_BASE + PL011_FBRD) = 1U;

    /* 8-bit, no parity, 1 stop bit, FIFO enabled */
    REG32(UART0_BASE + PL011_LCR_H) = PL011_LCR_WLEN8 | PL011_LCR_FEN;

    /* Mask all interrupts — we use polling */
    REG32(UART0_BASE + PL011_IMSC) = 0;

    /* Enable UART: TX + RX */
    REG32(UART0_BASE + PL011_CR) = PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE;
}

void uart_putc(char c)
{
    /* Auto newline: send CR before LF for terminal compatibility */
    if (c == '\n') {
        uart_putc('\r');
    }

    /* Wait until TX FIFO is not full before writing
     * PL011_FR.TXFF = 1 means TX FIFO is full — must wait */
    while (REG32(UART0_BASE + PL011_FR) & PL011_FR_TXFF)
        ;

    /* Write character to data register (TX FIFO) */
    REG32(UART0_BASE + PL011_DR) = (uint32_t)c;
}

/* ================================================================
 * PLATFORM_BBB — NS16550-compatible UART (AM335x UART0)
 *
 * Base: UART0_BASE = 0x44E09000U
 * ================================================================ */
#elif defined(PLATFORM_BBB)

/* NS16550 register offsets (8-bit registers, accessed as 32-bit) */
#define NS16550_RHR     0x00U   /* Receive Holding Register (read)  */
#define NS16550_THR     0x00U   /* Transmit Holding Register (write) */
#define NS16550_DLL     0x00U   /* Baud Divisor Low  (LCR.DLAB=1)  */
#define NS16550_DLH     0x04U   /* Baud Divisor High (LCR.DLAB=1)  */
#define NS16550_IER     0x04U   /* Interrupt Enable Register        */
#define NS16550_FCR     0x08U   /* FIFO Control Register (write)    */
#define NS16550_LCR     0x0CU   /* Line Control Register            */
#define NS16550_MCR     0x10U   /* Modem Control Register           */
#define NS16550_LSR     0x14U   /* Line Status Register             */
#define NS16550_MDR1    0xA4U   /* Mode Definition Register 1       */

/* LSR bits */
#define NS16550_LSR_THRE    (1U << 5)   /* TX Holding Register Empty */
#define NS16550_LSR_TEMT    (1U << 6)   /* TX Empty (shift reg + THR)*/

/* FCR bits */
#define NS16550_FCR_EN      (1U << 0)
#define NS16550_FCR_RXCLR   (1U << 1)
#define NS16550_FCR_TXCLR   (1U << 2)

/* LCR bits */
#define NS16550_LCR_8N1     0x03U       /* 8 data, no parity, 1 stop */
#define NS16550_LCR_DLAB    (1U << 7)   /* Divisor Latch Access Bit  */

/* MDR1 modes */
#define NS16550_MDR1_16X    0x00U       /* 16x oversampling (standard)*/
#define NS16550_MDR1_RESET  0x07U       /* Disable UART              */

void uart_init(void)
{
    /* Leave operational mode before reconfiguration. */
    REG32(UART0_BASE + NS16550_MDR1) = NS16550_MDR1_RESET;

    /* Early boot uses polling only. */
    REG32(UART0_BASE + NS16550_IER) = 0;

    /* Clear FIFO state. */
    REG32(UART0_BASE + NS16550_FCR) =
        NS16550_FCR_EN | NS16550_FCR_RXCLR | NS16550_FCR_TXCLR;

    /* Program baud divisor. */
    REG32(UART0_BASE + NS16550_LCR) = NS16550_LCR_DLAB | NS16550_LCR_8N1;
    REG32(UART0_BASE + NS16550_DLL) = 26U & 0xFFU;
    REG32(UART0_BASE + NS16550_DLH) = 0U;

    /* Return to normal 8N1 line format. */
    REG32(UART0_BASE + NS16550_LCR) = NS16550_LCR_8N1;

    /* Re-enter normal UART mode. */
    REG32(UART0_BASE + NS16550_MDR1) = NS16550_MDR1_16X;
}

void uart_putc(char c)
{
    if (c == '\n') {
        uart_putc('\r');
    }

    /* Wait for transmit space. */
    while (!(REG32(UART0_BASE + NS16550_LSR) & NS16550_LSR_THRE))
        ;

    REG32(UART0_BASE + NS16550_THR) = (uint32_t)c;
}

#else
#error "uart.c: unknown PLATFORM — define PLATFORM_QEMU or PLATFORM_BBB"
#endif /* PLATFORM_QEMU / PLATFORM_BBB */

/* ================================================================
 * Common — platform-independent helpers
 * ================================================================ */

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
