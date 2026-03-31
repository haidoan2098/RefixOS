/* ===========================================================
 * kernel/drivers/uart/uart.c — UART driver
 *
 * Supports two hardware backends selected at compile time:
 *   PLATFORM_QEMU → PL011 UART (ARM PrimeCell UART)
 *   PLATFORM_BBB  → NS16550 compatible (AM335x UART0)
 *
 * Platform-specific base addresses come from kernel/include/board.h.
 * =========================================================== */

#include <stdint.h>
#include <stdarg.h>
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

/*
 * uart_print_dec() — Print unsigned 32-bit integer in decimal.
 */
static void uart_print_dec(uint32_t val)
{
    char buf[10]; /* max 10 digits for uint32 */
    int i = 0;

    if (val == 0) {
        uart_putc('0');
        return;
    }
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (--i >= 0) {
        uart_putc(buf[i]);
    }
}

/*
 * uart_print_int() — Print signed 32-bit integer in decimal.
 */
static void uart_print_int(int32_t val)
{
    if (val < 0) {
        uart_putc('-');
        uart_print_dec((uint32_t)(-(val + 1)) + 1);
    } else {
        uart_print_dec((uint32_t)val);
    }
}

/*
 * uart_printf() — Minimal printf for kernel debug output.
 *
 * Supported format specifiers:
 *   %c   — character
 *   %s   — string
 *   %d   — signed decimal int
 *   %u   — unsigned decimal int
 *   %x   — hex (lowercase, no prefix)
 *   %p   — pointer (0x prefix, zero-padded 8 digits)
 *   %08x — zero-padded 8-digit hex (only 08 width supported)
 *   %%   — literal '%'
 */
void uart_printf(const char *fmt, ...)
{
    static const char hex[] = "0123456789abcdef";
    va_list ap;
    uint32_t uval;
    int i;

    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            uart_putc(*fmt++);
            continue;
        }
        fmt++; /* skip '%' */

        /* Check for %08x */
        if (fmt[0] == '0' && fmt[1] == '8' && fmt[2] == 'x') {
            uval = va_arg(ap, uint32_t);
            for (i = 28; i >= 0; i -= 4)
                uart_putc(hex[(uval >> i) & 0xFU]);
            fmt += 3;
            continue;
        }

        switch (*fmt) {
        case 'c':
            uart_putc((char)va_arg(ap, int));
            break;
        case 's':
            uart_puts(va_arg(ap, const char *));
            break;
        case 'd':
            uart_print_int(va_arg(ap, int32_t));
            break;
        case 'u':
            uart_print_dec(va_arg(ap, uint32_t));
            break;
        case 'x':
            uval = va_arg(ap, uint32_t);
            for (i = 28; i >= 0; i -= 4) {
                if ((uval >> i) & 0xFU || i == 0)
                    break;
            }
            for (; i >= 0; i -= 4)
                uart_putc(hex[(uval >> i) & 0xFU]);
            break;
        case 'p':
            uval = va_arg(ap, uint32_t);
            uart_puts("0x");
            for (i = 28; i >= 0; i -= 4)
                uart_putc(hex[(uval >> i) & 0xFU]);
            break;
        case '%':
            uart_putc('%');
            break;
        default:
            uart_putc('%');
            uart_putc(*fmt);
            break;
        }
        fmt++;
    }

    va_end(ap);
}
