/* ============================================================
 * kernel/drivers/uart/ns16550.c — NS16550-compatible UART driver
 *
 * Chip: NS16550 / 16550A — also covers the OMAP/AM335x variant.
 * TX via polling on LSR.THRE; RX via IRQ drained into the
 * subsystem's ring buffer. RX trigger = 1 byte so each keystroke
 * fires immediately (interactive latency > throughput).
 *
 * MDR1 register (0xA4) is OMAP-specific; benign on true NS16550.
 * ============================================================ */

#include <stdint.h>
#include "drivers/uart.h"

#define REG32(addr)  (*((volatile uint32_t *)(addr)))

#define NS16550_RHR     0x00U
#define NS16550_THR     0x00U
#define NS16550_DLL     0x00U
#define NS16550_DLH     0x04U
#define NS16550_IER     0x04U
#define NS16550_FCR     0x08U
#define NS16550_LCR     0x0CU
#define NS16550_LSR     0x14U
#define NS16550_MDR1    0xA4U

#define NS16550_LSR_DR      (1U << 0)
#define NS16550_LSR_THRE    (1U << 5)

#define NS16550_IER_RHR_IT  (1U << 0)

#define NS16550_FCR_EN      (1U << 0)
#define NS16550_FCR_RXCLR   (1U << 1)
#define NS16550_FCR_TXCLR   (1U << 2)
#define NS16550_FCR_RXTRIG1 (0U << 6)

#define NS16550_LCR_8N1     0x03U
#define NS16550_LCR_DLAB    (1U << 7)

#define NS16550_MDR1_16X    0x00U
#define NS16550_MDR1_RESET  0x07U

void scheduler_wake_reader(void);

static void ns16550_init(struct uart_device *dev)
{
    uint32_t base = dev->base;

    REG32(base + NS16550_MDR1) = NS16550_MDR1_RESET;
    REG32(base + NS16550_IER)  = 0;

    REG32(base + NS16550_FCR) = NS16550_FCR_EN
                              | NS16550_FCR_RXCLR
                              | NS16550_FCR_TXCLR
                              | NS16550_FCR_RXTRIG1;

    REG32(base + NS16550_LCR) = NS16550_LCR_DLAB | NS16550_LCR_8N1;
    REG32(base + NS16550_DLL) = 26U & 0xFFU;
    REG32(base + NS16550_DLH) = 0U;

    REG32(base + NS16550_LCR) = NS16550_LCR_8N1;

    REG32(base + NS16550_IER) = NS16550_IER_RHR_IT;

    REG32(base + NS16550_MDR1) = NS16550_MDR1_16X;
}

static void ns16550_putc(struct uart_device *dev, char c)
{
    uint32_t base = dev->base;

    while (!(REG32(base + NS16550_LSR) & NS16550_LSR_THRE))
        ;

    REG32(base + NS16550_THR) = (uint32_t)c;
}

static void ns16550_rx_irq(struct uart_device *dev)
{
    uint32_t base = dev->base;

    while (REG32(base + NS16550_LSR) & NS16550_LSR_DR) {
        char c = (char)(REG32(base + NS16550_RHR) & 0xFFU);
        uart_rx_push(c);
    }

    scheduler_wake_reader();
}

const struct uart_ops ns16550_ops = {
    .init   = ns16550_init,
    .putc   = ns16550_putc,
    .rx_irq = ns16550_rx_irq,
};
